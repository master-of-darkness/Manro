// Mesh shader: emits faces from the per-brick face cache (written by task
// on first sight / edits, re-scanned only when dirty). Each mesh group
// renders 32 faces (128 verts / 64 tris) — full 32-lane occupancy on full
// groups. Payload carries only brickIdx + faceCount; faces come from
// faceCacheAddr (see voxel_common).
import voxel_common;
import voxel_task;

[[vk::push_constant]]
VoxelFrameRoot g_MeshRoot;

struct VoxelMeshVertex {
    float4 pos : SV_Position;
    // Per-corner quad UV (u along the horizontal tangent, v=0 at the
    // texture top row). Interpolated; the fragment samples the tile array.
    float2 uv : TEXCOORD0;
    // Flat (face:3 | state:15): tile + cutout resolve in fragment.
    nointerpolation uint faceState : TEXCOORD1;
};

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void MeshMain(
    uint gtid : SV_DispatchThreadID,
    uint gidx : SV_GroupIndex,
    uint3 mgrid : SV_GroupID,
    out vertices VoxelMeshVertex verts[128],
    out indices uint3 tris[64],
    payload VoxelTaskPayload payload) {
    vk::BufferPointer<VoxelFrameParams> fp = vk::BufferPointer<VoxelFrameParams>(g_MeshRoot.frameAddr);
    VoxelFrameParams* fpp = fp.getPtr();
    VoxelFrameParams frame = fpp[0];

    vk::BufferPointer<uint> mdbg = vk::BufferPointer<uint>(g_MeshRoot.debugAddr);
    uint* mdebug = mdbg.getPtr();
    if (gidx == 0 && frame.debugEnabled != 0u) {
        InterlockedAdd(mdebug[4], 1u); // mesh groups run
        InterlockedAdd(mdebug[5], payload.faceCount);
    }

    vk::BufferPointer<VoxelBrickHeader> headers = vk::BufferPointer<VoxelBrickHeader>(g_MeshRoot.headerAddr);
    VoxelBrickHeader* h = headers.getPtr();
    VoxelBrickHeader hdr = h[payload.brickIdx];

    float voxelSize = frame.brickSize / 16.0;

    // This group owns faces [base, base+32); lane-parallel emission: lane
    // gidx owns face gidx, so full groups run 32/32 lanes.
    // Counts stay uniform across lanes, so SetMeshOutputCounts is uniform;
    // only the per-face writes diverge, which is legal.
    uint base = mgrid.x * kVoxelFacesPerMeshGroup;
    uint count = min(kVoxelFacesPerMeshGroup, payload.faceCount - min(base, payload.faceCount));

    // Faces live in the cache: [0] = faceCount, [1..] = packed faces.
    vk::BufferPointer<uint> fcache = vk::BufferPointer<uint>(g_MeshRoot.faceCacheAddr);
    uint* fcb = fcache.getPtr() + payload.brickIdx * kVoxelFaceCacheStride;

    uint vertCount = count * 4u;
    uint triCount = count * 2u;
    // Per-frame backface rejection (runs here, not in task: the face cache
    // is viewpoint-independent). Culled faces emit degenerate quads —
    // zero-area tris the rasterizer discards; counts stay uniform.
    // Disabled when the camera is inside the brick (no holes).
    bool camInside =
        all(frame.cameraPos >= hdr.origin - voxelSize) &&
        all(frame.cameraPos <= hdr.origin + frame.brickSize + voxelSize);
    if (gidx < count) {
        uint packed = fcb[1u + base + gidx];
        uint iter = packed & 0xFFFu;
        uint f = (packed >> 12u) & 0x7u;
        uint mat = (packed >> 15u) & 0x7FFFu;
        uint x = iter & 15u;
        uint y = (iter >> 4u) & 15u;
        uint z = (iter >> 8u) & 15u;

        float3 b;
        float3 nrm = kFaceNormal[f];
        // Shrunken quad for the block's baked shape (trapdoor slabs, door
        // panels, bamboo stalks, fence posts, ...). Full shapes reproduce
        // the old full-voxel quads exactly; corner order matches, so the
        // per-face axis mapping below is untouched.
        float3 t1 = abs(nrm.x) > 0.5 ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 t2 = abs(nrm.x) > 0.5 ? float3(0, 0, 1)
                    : (abs(nrm.y) > 0.5 ? float3(0, 0, 1) : float3(0, 1, 0));
        float3 e1w = t1 * voxelSize;
        float3 e2w = t2 * voxelSize;
        // Baked uv sub-rect of the face's tile (full-rect = old full-tile
        // sampling). Sampling the author's rect instead of stretching the
        // whole tile keeps cutout discard where vanilla discards (a
        // stretched full texture smears transparent rows across thin
        // faces and erases them).
        float4 uvRect = float4(0.0, 0.0, 1.0, 1.0);
        {
            vk::BufferPointer<uint> shapePtr = vk::BufferPointer<uint>(g_MeshRoot.shapeTableAddr);
            uint* shapes = shapePtr.getPtr();
            uint ms = min(mat, kBlockStateMax - 1u);
            int3 sMin = VoxelShapeMin(shapes, ms);
            int3 sMax = VoxelShapeMax(shapes, ms);
            float3 qb, qe1, qe2;
            VoxelFaceQuad(x, y, z, f, sMin, sMax, qb, qe1, qe2);
            // World-space base + edges of the shape's face rect (full
            // shapes reproduce b / t1*voxelSize / t2*voxelSize exactly).
            // (Winding is irrelevant: the PSO uses CULL_MODE_NONE.)
            b = hdr.origin + qb * voxelSize;
            e1w = qe1 * voxelSize;
            e2w = qe2 * voxelSize;
            vk::BufferPointer<uint> uvPtr = vk::BufferPointer<uint>(g_MeshRoot.uvTableAddr);
            uvRect = VoxelFaceUv(uvPtr.getPtr(), ms, f);
        }
        float3 corners[4] = {
            b, b + e1w, b + e1w + e2w, b + e2w
        };
        uint vtx = gidx * 4u;
        // Quad UVs: (uu, vv) are the corner fractions along the fixed
        // per-face axes (u along the horizontal tangent, v=0 at the top
        // row; Vulkan v=0 is the PNG's first row). They lerp inside the
        // baked sub-rect, so each face samples the texels the model maps
        // there instead of a stretched full tile.
        // ±Y faces: t1=+X (u), t2=+Z; ±Z faces: t1=+X (u), t2=+Y;
        // ±X faces: t1=+Y, t2=+Z (u along t2).
        bool uAlongT1 = abs(nrm.x) < 0.5;
        // Backface test uses the voxel center (conservative: keeps grazing
        // faces, never drops a visible one).
        float3 vc = hdr.origin + (float3(float(x), float(y), float(z)) + 0.5) * voxelSize;
        bool culled = frame.useBackface != 0u && !camInside &&
                      dot(nrm, frame.cameraPos - vc) <= 0.0;
        float3 anchor = culled ? vc : corners[0];
        uint faceState = ((mat & 0x7FFFu) << 3u) | (f & 0x7u);
        for (uint c = 0u; c < 4u; ++c) {
            float o1 = (c == 1u || c == 2u) ? 1.0 : 0.0;
            float o2 = (c >= 2u) ? 1.0 : 0.0;
            float uu = uAlongT1 ? o1 : o2;
            float vv;
            if (abs(nrm.y) > 0.5) {
                vv = o2; // top/bottom faces: v along +Z
            } else if (abs(t1.y) > 0.5) {
                vv = 1.0 - o1; // t1 vertical: v along +Y, top row first
            } else {
                vv = 1.0 - o2; // t2 vertical: v along +Y, top row first
            }
            verts[vtx + c].pos =
                mul(frame.viewProj, float4(culled ? anchor : corners[c], 1.0));
            verts[vtx + c].uv =
                float2(uvRect.x + (uvRect.z - uvRect.x) * uu,
                       uvRect.y + (uvRect.w - uvRect.y) * vv);
            verts[vtx + c].faceState = faceState;
        }
        tris[gidx * 2u] = uint3(vtx, vtx + 1, vtx + 2);
        tris[gidx * 2u + 1u] = uint3(vtx, vtx + 2u, vtx + 3u);
    }

    SetMeshOutputCounts(vertCount, triCount);
}
