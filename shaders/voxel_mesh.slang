import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_MeshRoot;

struct VoxelMeshVertex {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint faceState : TEXCOORD1;
    nointerpolation uint tile : TEXCOORD2;
    nointerpolation uint mflags : TEXCOORD3;
};

groupshared float4x4 s_viewProj;
groupshared float3 s_camPos;
groupshared float3 s_origin;
groupshared float s_voxelSize;
groupshared float s_brickSize;
groupshared uint s_useBackface;
groupshared uint s_debugOn;
groupshared uint s_widx[32];
groupshared uint s_totalVis;

static const float3 kFaceT1[6] = {
    float3(0, 1, 0), float3(0, 1, 0),
    float3(1, 0, 0), float3(1, 0, 0),
    float3(1, 0, 0), float3(1, 0, 0),
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
    if (gidx == 0) {
        vk::BufferPointer<VoxelFrameParams> fp =
            vk::BufferPointer<VoxelFrameParams>(g_MeshRoot.frameAddr);
        VoxelFrameParams frame = fp.getPtr()[0];
        s_viewProj = frame.viewProj;
        s_camPos = frame.cameraPos;
        s_brickSize = frame.brickSize;
        s_voxelSize = frame.brickSize / 16.0;
        s_useBackface = frame.useBackface;
        s_debugOn = frame.debugEnabled;
        vk::BufferPointer<VoxelBrickHeader> headers =
            vk::BufferPointer<VoxelBrickHeader>(g_MeshRoot.headerAddr);
        s_origin = headers.getPtr()[payload.brickIdx].origin;
    }
    GroupMemoryBarrierWithGroupSync();

    float voxelSize = s_voxelSize;
    float3 bOrigin = s_origin;

    if (gidx == 0 && s_debugOn != 0u) {
        vk::BufferPointer<uint> mdbg = vk::BufferPointer<uint>(g_MeshRoot.debugAddr);
        InterlockedAdd(mdbg.getPtr()[4], 1u);
        InterlockedAdd(mdbg.getPtr()[5], payload.faceCount);
    }

    uint base = mgrid.x * kVoxelFacesPerMeshGroup;
    uint remaining = payload.faceCount > base ? payload.faceCount - base : 0u;
    uint count = min(kVoxelFacesPerMeshGroup, remaining);

    vk::BufferPointer<uint> fcache = vk::BufferPointer<uint>(g_MeshRoot.faceCacheAddr);
    uint* fcb = fcache.getPtr() + payload.brickIdx * kVoxelFaceCacheStride;

    uint packed = 0u;
    uint f = 0u;
    uint mat = 0u;
    uint iter = 0u;
    bool hasFace = gidx < count;
    bool vis = false;
    float3 nrm = float3(0.0);
    if (hasFace) {
        packed = fcb[1u + base + gidx];
        iter = packed & 0xFFFu;
        f = (packed >> 12u) & 0x7u;
        mat = (packed >> 15u) & 0x7FFFu;
        nrm = kFaceNormal[f];
        uint x = iter & 15u;
        uint y = (iter >> 4u) & 15u;
        uint z = (iter >> 8u) & 15u;
        bool camInside =
            all(s_camPos >= bOrigin - voxelSize) &&
            all(s_camPos <= bOrigin + s_brickSize + voxelSize);
        vis = true;
        if (s_useBackface != 0u && !camInside) {
            vk::BufferPointer<uint> shapePtr =
                vk::BufferPointer<uint>(g_MeshRoot.shapeTableAddr);
            uint ms = min(mat, kBlockStateMax - 1u);
            int3 sMin = VoxelShapeMin(shapePtr.getPtr(), ms);
            int3 sMax = VoxelShapeMax(shapePtr.getPtr(), ms);
            uint axis = f >> 1u;
            int off = ((f & 1u) == 0u) ? sMax[axis] : sMin[axis];
            float cellC = (axis == 0u) ? float(x) : (axis == 1u) ? float(y) : float(z);
            float orgC = (axis == 0u) ? bOrigin.x : (axis == 1u) ? bOrigin.y : bOrigin.z;
            float camC = (axis == 0u) ? s_camPos.x : (axis == 1u) ? s_camPos.y : s_camPos.z;
            float plane = orgC + (cellC + float(off) / 16.0) * voxelSize;
            vis = ((f & 1u) == 0u) ? (camC > plane) : (camC < plane);
        }
    }

    s_widx[gidx] = (hasFace && vis) ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();
    if (gidx == 0) {
        uint acc = 0u;
        for (uint i = 0u; i < 32u; ++i) {
            uint v = s_widx[i];
            s_widx[i] = acc;
            acc += v;
        }
        s_totalVis = acc;
    }
    GroupMemoryBarrierWithGroupSync();
    uint writeIdx = s_widx[gidx];
    uint totalVis = s_totalVis;

    uint vertCount = totalVis * 4u;
    uint triCount = totalVis * 2u;

    if (hasFace && vis) {
        uint x = iter & 15u;
        uint y = (iter >> 4u) & 15u;
        uint z = (iter >> 8u) & 15u;

        float3 b;
        float3 e1w;
        float3 e2w;
        float4 uvRect;
        {
            vk::BufferPointer<uint> shapePtr = vk::BufferPointer<uint>(g_MeshRoot.shapeTableAddr);
            uint* shapes = shapePtr.getPtr();
            uint ms = min(mat, kBlockStateMax - 1u);
            int3 sMin = VoxelShapeMin(shapes, ms);
            int3 sMax = VoxelShapeMax(shapes, ms);
            float3 qb, qe1, qe2;
            VoxelFaceQuad(x, y, z, f, sMin, sMax, qb, qe1, qe2);
            b = bOrigin + qb * voxelSize;
            e1w = qe1 * voxelSize;
            e2w = qe2 * voxelSize;
            vk::BufferPointer<uint> uvPtr = vk::BufferPointer<uint>(g_MeshRoot.uvTableAddr);
            uvRect = VoxelFaceUv(uvPtr.getPtr(), ms, f);
        }
        uint tile = 0u;
        uint mflags = 0u;
        {
            vk::BufferPointer<uint> tilePtr = vk::BufferPointer<uint>(g_MeshRoot.tileTableAddr);
            vk::BufferPointer<uint> flagPtr = vk::BufferPointer<uint>(g_MeshRoot.blockFlagsAddr);
            uint ms2 = min(mat, kBlockStateMax - 1u);
            tile = tilePtr.getPtr()[ms2 * 6u + f];
            mflags = flagPtr.getPtr()[ms2];
        }

        float3 corners0 = b;
        float3 corners1 = b + e1w;
        float3 corners2 = b + e1w + e2w;
        float3 corners3 = b + e2w;

        bool uAlongT1 = abs(nrm.x) < 0.5;
        float uu0, uu1, uu2, uu3, vv0, vv1, vv2, vv3;
        {
            float o10 = 0.0, o11 = 1.0, o12 = 1.0, o13 = 0.0;
            float o20 = 0.0, o21 = 0.0, o22 = 1.0, o23 = 1.0;
            uu0 = uAlongT1 ? o10 : o20;
            uu1 = uAlongT1 ? o11 : o21;
            uu2 = uAlongT1 ? o12 : o22;
            uu3 = uAlongT1 ? o13 : o23;
            if (abs(nrm.y) > 0.5) {
                vv0 = o20; vv1 = o21; vv2 = o22; vv3 = o23;
            } else if (abs(kFaceT1[f].y) > 0.5) {
                vv0 = 1.0 - o10; vv1 = 1.0 - o11; vv2 = 1.0 - o12; vv3 = 1.0 - o13;
            } else {
                vv0 = 1.0 - o20; vv1 = 1.0 - o21; vv2 = 1.0 - o22; vv3 = 1.0 - o23;
            }
        }

        uint vtx = writeIdx * 4u;
        uint faceState = ((mat & 0x7FFFu) << 3u) | (f & 0x7u);
        float4x4 vp = s_viewProj;
        verts[vtx + 0].pos = mul(vp, float4(corners0, 1.0));
        verts[vtx + 1].pos = mul(vp, float4(corners1, 1.0));
        verts[vtx + 2].pos = mul(vp, float4(corners2, 1.0));
        verts[vtx + 3].pos = mul(vp, float4(corners3, 1.0));
        verts[vtx + 0].uv = float2(uvRect.x + (uvRect.z - uvRect.x) * uu0,
                                   uvRect.y + (uvRect.w - uvRect.y) * vv0);
        verts[vtx + 1].uv = float2(uvRect.x + (uvRect.z - uvRect.x) * uu1,
                                   uvRect.y + (uvRect.w - uvRect.y) * vv1);
        verts[vtx + 2].uv = float2(uvRect.x + (uvRect.z - uvRect.x) * uu2,
                                   uvRect.y + (uvRect.w - uvRect.y) * vv2);
        verts[vtx + 3].uv = float2(uvRect.x + (uvRect.z - uvRect.x) * uu3,
                                   uvRect.y + (uvRect.w - uvRect.y) * vv3);
        verts[vtx + 0].faceState = faceState;
        verts[vtx + 1].faceState = faceState;
        verts[vtx + 2].faceState = faceState;
        verts[vtx + 3].faceState = faceState;
        verts[vtx + 0].tile = tile;
        verts[vtx + 1].tile = tile;
        verts[vtx + 2].tile = tile;
        verts[vtx + 3].tile = tile;
        verts[vtx + 0].mflags = mflags;
        verts[vtx + 1].mflags = mflags;
        verts[vtx + 2].mflags = mflags;
        verts[vtx + 3].mflags = mflags;
        tris[writeIdx * 2u] = uint3(vtx, vtx + 1, vtx + 2);
        tris[writeIdx * 2u + 1u] = uint3(vtx, vtx + 2u, vtx + 3u);
    }

    SetMeshOutputCounts(vertCount, triCount);
}
