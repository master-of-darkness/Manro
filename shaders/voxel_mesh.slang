// Mesh shader: emits faces from the per-brick face cache (written by task
// on first sight / edits, re-scanned only when dirty). Each mesh group
// renders 16 faces (64 verts / 32 tris). Payload carries only brickIdx +
// faceCount; faces come from faceCacheAddr (see voxel_common).
import voxel_common;
import voxel_task;

[[vk::push_constant]]
VoxelFrameRoot g_MeshRoot;

struct VoxelMeshVertex {
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    nointerpolation uint packed : PACKED;
    nointerpolation uint material : MATERIAL;
};

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void MeshMain(
    uint gtid : SV_DispatchThreadID,
    uint gidx : SV_GroupIndex,
    uint3 mgrid : SV_GroupID,
    out vertices VoxelMeshVertex verts[64],
    out indices uint3 tris[32],
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

    // This group owns faces [base, base+16). Lane-parallel emission: lane
    // gidx owns face gidx (16 of 32 lanes active instead of just lane 0).
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
        uint mat = (packed >> 15u) & 0x1FFu;
        uint x = iter & 15u;
        uint y = (iter >> 4u) & 15u;
        uint z = (iter >> 8u) & 15u;

        float3 b = hdr.origin + (float3(float(x), float(y), float(z)) + max(kFaceNormal[f], float3(0, 0, 0))) * voxelSize;
        float3 nrm = kFaceNormal[f];
        // Face tangents: the two non-normal +axes, so the quad spans
        // exactly the voxel's face square. (The old v = cross(n, u)
        // points down the negative tangent for -X/+Y/+Z, shifting those
        // quads a full voxel and leaving open edge strips = holes.
        // Winding is irrelevant: the PSO uses CULL_MODE_NONE.)
        float3 t1 = abs(nrm.x) > 0.5 ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 t2 = abs(nrm.x) > 0.5 ? float3(0, 0, 1)
                    : (abs(nrm.y) > 0.5 ? float3(0, 0, 1) : float3(0, 1, 0));
        float3 corners[4] = {
            b, b + t1 * voxelSize, b + t1 * voxelSize + t2 * voxelSize, b + t2 * voxelSize
        };
        uint vtx = gidx * 4u;
        // Face-ambient-occlusion stub: wired in Phase 6 (GI), flat 3 now.
        // Backface test uses the voxel center (conservative: keeps grazing
        // faces, never drops a visible one).
        float3 vc = hdr.origin + (float3(float(x), float(y), float(z)) + 0.5) * voxelSize;
        bool culled = frame.useBackface != 0u && !camInside &&
                      dot(nrm, frame.cameraPos - vc) <= 0.0;
        float3 anchor = culled ? vc : corners[0];
        for (uint c = 0u; c < 4u; ++c) {
            verts[vtx + c].pos =
                mul(frame.viewProj, float4(culled ? anchor : corners[c], 1.0));
            verts[vtx + c].normal = nrm;
            verts[vtx + c].packed = PackVoxelVertex(x, y, z, f, 3u, mat);
            verts[vtx + c].material = mat;
        }
        tris[gidx * 2u] = uint3(vtx, vtx + 1, vtx + 2);
        tris[gidx * 2u + 1u] = uint3(vtx, vtx + 2u, vtx + 3u);
    }

    SetMeshOutputCounts(vertCount, triCount);
}
