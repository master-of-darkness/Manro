// Task (amplification) shader: per-brick cull + face compaction.
// One task group per resident brick. Survivors compact the FULL exposed-face
// list into the payload, then fan out to ceil(faceCount/16) mesh groups.
// Dense-brick worst case ~1536 faces -> 96 mesh groups.
import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_TaskRoot;

// Module-scope shared staging for face compaction (32 lanes cooperate).
// s_faceStaging layout: [0] = count, [1..2048] = packed faces.
groupshared uint s_faceStaging[2049];
groupshared uint s_hasContent;

VoxelFrameParams LoadFrame() {
    vk::BufferPointer<VoxelFrameParams> fp = vk::BufferPointer<VoxelFrameParams>(g_TaskRoot.frameAddr);
    VoxelFrameParams* p = fp.getPtr();
    return p[0];
}

float4 ExtractPlane(float4x4 vp, int axis, int sign) {
    float4 row0 = float4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    float4 row1 = float4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    float4 row2 = float4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    float4 row3 = float4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
    float4 rows[3] = { row0, row1, row2 };
    return row3 + float(sign) * rows[axis];
}

bool BrickVisible(uint brickIdx, VoxelFrameParams frame) {
    // NOTE: brickIdx here is the RESIDENT index (0..brickCount), not the
    // virtual slot. The task dispatch covers resident bricks only, so the
    // page-table probe is a residency sanity check, not the mapping.
    if (brickIdx >= frame.brickCount)
        return false;

    vk::BufferPointer<VoxelBrickHeader> headers = vk::BufferPointer<VoxelBrickHeader>(g_TaskRoot.headerAddr);
    VoxelBrickHeader* h = headers.getPtr();
    VoxelBrickHeader hdr = h[brickIdx];
    if ((hdr.flags & 1u) == 0u)
        return false;

    float3 center = hdr.origin + float3(frame.brickSize * 0.5);
    float radius = length(float3(frame.brickSize * 0.5));

    float3 toCenter = center - frame.cameraPos;
    float maxDist = float(frame.maxDrawDistance) + radius;
    if (dot(toCenter, toCenter) > maxDist * maxDist)
        return false;

    // Sphere-vs-frustum, planes extracted from viewProj rows.
    for (int axis = 0; axis < 3; ++axis) {
        for (int s = -1; s <= 1; s += 2) {
            float4 plane = ExtractPlane(frame.viewProj, axis, s);
            float len = length(plane.xyz);
            plane /= max(len, 1e-6);
            if (dot(plane.xyz, center) + plane.w < -radius)
                return false;
        }
    }
    return true;
}

// BISECT DEBUG: synthetic full top-plane faces, bypasses ALL brick-data
// BDA reads (occupancy/materials). If the full 16x16 top plane appears,
// task->mesh->raster->composite is proven good and the bug is in brick
// data reads (sparse upload / BDA / occupancy). Flip to false to revert.
static const bool kDebugSyntheticFaces = false;

[shader("amplification")]
[numthreads(32, 1, 1)]
void TaskMain(uint gidx : SV_GroupIndex, uint3 gid : SV_GroupID) {
    VoxelFrameParams frame = LoadFrame();
    uint brickIdx = gid.x;

    // DEBUG counter helper: debugAddr[0] = task runs, [1] = visible bricks,
    // [2] = total compacted faces. Skipped unless frame.debugEnabled.
    vk::BufferPointer<uint> dbg = vk::BufferPointer<uint>(g_TaskRoot.debugAddr);
    uint* debug = dbg.getPtr();
    bool wantDbg = frame.debugEnabled != 0u;
    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[0], 1u);
    }

    if (!BrickVisible(brickIdx, frame)) {
        if (gidx == 0 && wantDbg) {
            InterlockedAdd(debug[3], 1u); // culled
        }
        return;
    }
    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[1], 1u);
    }

    if (kDebugSyntheticFaces) {
        // 16x16 top faces (+Y, f=2) at voxel y=7, checkerboard mats.
        // 256 faces -> 16 mesh groups. Uniform across lanes.
        // DEBUG counters: synthetic path reports through the same slots.
        if (gidx == 0 && wantDbg) {
            InterlockedAdd(debug[2], 256u);
        }
        VoxelTaskPayload p;
        p.brickIdx = brickIdx;
        p.faceCount = 256u;
        for (uint i = 0u; i < 256u; ++i) {
            uint x = i & 15u;
            uint z = (i >> 4u) & 15u;
            uint iter = x + 7u * 16u + z * 256u;
            uint mat = (x + z) & 1u;
            p.faces[i] = (iter & 0xFFFu) | (2u << 12u) | ((mat & 0x1FFu) << 15u);
        }
        DispatchMesh(16u, 1, 1, p);
        return;
    }

    vk::BufferPointer<uint> bricks = vk::BufferPointer<uint>(g_TaskRoot.brickBufferAddr);
    uint* base = bricks.getPtr();
    uint* mats = base + brickIdx * 2176u;
    uint* occupancy = mats + 2048u;

    // Empty-brick early-out: 128 occupancy words, 4 per lane. Avoids the
    // full 4096-voxel x 6-face scan for air bricks (common at scale).
    // Uniform exit after the barrier — all lanes observe s_hasContent.
    if (gidx == 0) {
        s_faceStaging[0] = 0u;
        s_hasContent = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint w = gidx; w < 128u; w += 32u) {
        if (occupancy[w] != 0u) {
            InterlockedOr(s_hasContent, 1u);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (s_hasContent == 0u)
        return;

    // Backface rejection prep (uniform for the whole brick): brick origin
    // + camera-inside test. Faces pointing away from the camera are
    // skipped per-voxel below; disabled when the camera is inside the
    // brick so interior views keep all faces (no holes).
    vk::BufferPointer<VoxelBrickHeader> bheaders =
        vk::BufferPointer<VoxelBrickHeader>(g_TaskRoot.headerAddr);
    VoxelBrickHeader bhdr = bheaders.getPtr()[brickIdx];
    float voxelSize = frame.brickSize / 16.0;
    bool camInside =
        all(frame.cameraPos >= bhdr.origin - voxelSize) &&
        all(frame.cameraPos <= bhdr.origin + frame.brickSize + voxelSize);

    for (uint iter = gidx; iter < 4096u; iter += 32u) {
        uint x = iter & 15u;
        uint y = (iter >> 4u) & 15u;
        uint z = (iter >> 8u) & 15u;
        if (!VoxelSolid(occupancy, x, y, z))
            continue;
        uint mat = VoxelMaterial(mats, x, y, z);
        // Voxel center in world space (conservative cull plane: keeps
        // grazing faces, never drops a visible one).
        float3 vc = bhdr.origin + (float3(float(x), float(y), float(z)) + 0.5) * voxelSize;
        float3 toCam = frame.cameraPos - vc;
        for (uint f = 0u; f < 6u; ++f) {
            int3 n = int3(x, y, z) + kFaceOffset[f];
            if (VoxelSolid(occupancy, uint(n.x), uint(n.y), uint(n.z)))
                continue;
            if (!camInside && dot(kFaceNormal[f], toCam) <= 0.0)
                continue; // backfacing opaque quad — HW would cull it later
            uint slot;
            InterlockedAdd(s_faceStaging[0], 1u, slot);
            if (slot < 2048u) {
                s_faceStaging[1u + slot] = (iter & 0xFFFu) | ((f & 0x7u) << 12u) | ((mat & 0x1FFu) << 15u);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint faceCount = min(s_faceStaging[0], 2048u);
    // Uniform emit: every lane builds the identical payload from shared
    // staging, then all lanes call DispatchMesh with the same args.
    // (Divergent DispatchMesh / partial payloads are undefined behavior.)
    if (faceCount == 0u)
        return;
    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[2], faceCount);
    }
    VoxelTaskPayload p;
    p.brickIdx = brickIdx;
    p.faceCount = faceCount;
    for (uint i = 0u; i < faceCount; ++i) {
        p.faces[i] = s_faceStaging[1u + i];
    }
    uint meshGroups = (faceCount + kVoxelFacesPerMeshGroup - 1u) / kVoxelFacesPerMeshGroup;
    DispatchMesh(meshGroups, 1, 1, p);
}
