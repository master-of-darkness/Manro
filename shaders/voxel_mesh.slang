import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_MeshRoot;

struct VoxelMeshVertex {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint faceState : TEXCOORD1;
    nointerpolation uint tile : TEXCOORD2;
    nointerpolation uint mflags : TEXCOORD3;
    float3 vLight : TEXCOORD4;
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
        InterlockedAdd(mdbg.getPtr()[5], payload.faceCount + payload.spillCount);
    }

    uint total = payload.faceCount + payload.spillCount;
    uint lane = mgrid.x * kVoxelFacesPerMeshGroup + gidx;

    vk::BufferPointer<uint> fcache = vk::BufferPointer<uint>(g_MeshRoot.faceCacheAddr);
    uint* fcb = fcache.getPtr() + payload.brickIdx * kVoxelFaceCacheStride;
    vk::BufferPointer<uint> spillBuf = vk::BufferPointer<uint>(g_MeshRoot.spillAddr);
    uint* spillLane = spillBuf.getPtr() + payload.brickIdx * (kVoxelSpillQuota * 3u);

    uint packed = 0u;
    uint lightW1 = 0u;
    uint lightW2 = 0u;
    uint f = 0u;
    uint mat = 0u;
    uint iter = 0u;
    uint earlyFlags = 0u;
    bool hasFace = lane < total;
    bool vis = false;
    float3 nrm = float3(0.0);



    uint boxW3 = 0u;
    bool isMulti = false;
    int3 bMin = int3(0, 0, 0);
    int3 bMax = int3(16, 16, 16);
    if (hasFace) {
        if (lane < payload.faceCount) {
            uint faceIdx = payload.faceStart + lane;
            packed = fcb[1u + faceIdx * 3u];
            lightW1 = fcb[1u + faceIdx * 3u + 1u];
            lightW2 = fcb[1u + faceIdx * 3u + 2u];
        } else {
            uint sIdx = payload.spillStart + (lane - payload.faceCount);
            packed = spillLane[sIdx * 3u];
            lightW1 = spillLane[sIdx * 3u + 1u];
            lightW2 = spillLane[sIdx * 3u + 2u];
        }
        boxW3 = lightW2 >> 10u;
        iter = packed & 0xFFFu;
        f = (packed >> 12u) & 0x7u;
        mat = (packed >> 15u) & 0x7FFFu;
        nrm = kFaceNormal[f];
        uint x = iter & 15u;
        uint y = (iter >> 4u) & 15u;
        uint z = (iter >> 8u) & 15u;
        uint msB = min(mat, kBlockStateMax - 1u);
        if (boxW3 != 0u) {
            vk::BufferPointer<uint> boxPtr =
                vk::BufferPointer<uint>(g_MeshRoot.boxTableAddr);
            uint* boxes = boxPtr.getPtr();
            uint nBox = VoxelBoxCount(boxes, msB);
            uint bi = boxW3 - 1u;
            if (bi < nBox && nBox > 1u) {
                isMulti = true;
                bMin = VoxelBoxMin(boxes, msB, bi);
                bMax = VoxelBoxMax(boxes, msB, bi);
            }
        }
        bool camInside =
            all(s_camPos >= bOrigin - voxelSize) &&
             all(s_camPos <= bOrigin + s_brickSize + voxelSize);
        vis = true;
        {
            vk::BufferPointer<uint> flagPtr = vk::BufferPointer<uint>(g_MeshRoot.blockFlagsAddr);
            earlyFlags = flagPtr.getPtr()[min(mat, kBlockStateMax - 1u)];
        }
        bool isLeaf = (earlyFlags & 32u) != 0u;
        bool isTrapdoor = (earlyFlags & 64u) != 0u;
        if (!isLeaf && s_useBackface != 0u && (!camInside || isTrapdoor)) {
            int3 sMin;
            int3 sMax;
            if (isMulti) {
                sMin = bMin;
                sMax = bMax;
            } else {
                vk::BufferPointer<uint> shapePtr =
                    vk::BufferPointer<uint>(g_MeshRoot.shapeTableAddr);
                uint ms = min(mat, kBlockStateMax - 1u);
                sMin = VoxelShapeMin(shapePtr.getPtr(), ms);
                sMax = VoxelShapeMax(shapePtr.getPtr(), ms);
            }
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
        uint tile = 0u;
        uint mflags = earlyFlags;
        if (!isMulti) {
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
            vk::BufferPointer<uint> tilePtr = vk::BufferPointer<uint>(g_MeshRoot.tileTableAddr);
            tile = tilePtr.getPtr()[ms * 6u + f];
        } else {




            float3 qb, qe1, qe2;
            VoxelFaceQuad(x, y, z, f, bMin, bMax, qb, qe1, qe2);
            b = bOrigin + qb * voxelSize;
            e1w = qe1 * voxelSize;
            e2w = qe2 * voxelSize;
            vk::BufferPointer<uint> boxPtr2 =
                vk::BufferPointer<uint>(g_MeshRoot.boxTableAddr);
            uint msb = min(mat, kBlockStateMax - 1u);
            tile = VoxelBoxPlank(boxPtr2.getPtr(), msb);
            if (f == 2u || f == 3u) {
                uvRect = float4(float(bMin.x), float(bMin.z),
                                float(bMax.x), float(bMax.z)) / 16.0;
            } else if (f == 0u || f == 1u) {
                uvRect = float4(float(bMin.z), float(16 - bMax.y),
                                float(bMax.z), float(16 - bMin.y)) / 16.0;
            } else {
                uvRect = float4(float(bMin.x), float(16 - bMax.y),
                                float(bMax.x), float(16 - bMin.y)) / 16.0;
            }
        }



        float vSky[4];
        float vBlk[4];
        float vAo[4];
        {
            uint w[4];
            w[0] = lightW1 & 0x3FFu;
            w[1] = (lightW1 >> 10u) & 0x3FFu;
            w[2] = (lightW1 >> 20u) & 0x3FFu;
            w[3] = lightW2 & 0x3FFu;
            for (uint ci = 0u; ci < 4u; ++ci) {
                vSky[ci] = float(w[ci] & 0xFu);
                vBlk[ci] = float((w[ci] >> 4u) & 0xFu);
                vAo[ci] = kAoCurve[(w[ci] >> 8u) & 0x3u];
            }
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
        verts[vtx + 0].vLight = float3(vSky[0], vBlk[0], vAo[0]);
        verts[vtx + 1].vLight = float3(vSky[1], vBlk[1], vAo[1]);
        verts[vtx + 2].vLight = float3(vSky[2], vBlk[2], vAo[2]);
        verts[vtx + 3].vLight = float3(vSky[3], vBlk[3], vAo[3]);
        tris[writeIdx * 2u] = uint3(vtx, vtx + 1, vtx + 2);
        tris[writeIdx * 2u + 1u] = uint3(vtx, vtx + 2u, vtx + 3u);
    }

    SetMeshOutputCounts(vertCount, triCount);
}
