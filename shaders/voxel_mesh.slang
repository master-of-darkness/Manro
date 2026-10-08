import voxel_common;
import voxel_task;

[[vk::push_constant]]
VoxelFrameRoot g_MeshRoot;

struct VoxelMeshVertex {
    float4 pos : SV_Position;

    float2 uv : TEXCOORD0;

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
        InterlockedAdd(mdebug[4], 1u);
        InterlockedAdd(mdebug[5], payload.faceCount);
    }

    vk::BufferPointer<VoxelBrickHeader> headers = vk::BufferPointer<VoxelBrickHeader>(g_MeshRoot.headerAddr);
    VoxelBrickHeader* h = headers.getPtr();
    VoxelBrickHeader hdr = h[payload.brickIdx];

    float voxelSize = frame.brickSize / 16.0;

    uint base = mgrid.x * kVoxelFacesPerMeshGroup;
    uint count = min(kVoxelFacesPerMeshGroup, payload.faceCount - min(base, payload.faceCount));

    vk::BufferPointer<uint> fcache = vk::BufferPointer<uint>(g_MeshRoot.faceCacheAddr);
    uint* fcb = fcache.getPtr() + payload.brickIdx * kVoxelFaceCacheStride;

    uint vertCount = count * 4u;
    uint triCount = count * 2u;

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

        float3 t1 = abs(nrm.x) > 0.5 ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 t2 = abs(nrm.x) > 0.5 ? float3(0, 0, 1)
                    : (abs(nrm.y) > 0.5 ? float3(0, 0, 1) : float3(0, 1, 0));
        float3 e1w = t1 * voxelSize;
        float3 e2w = t2 * voxelSize;

        float4 uvRect = float4(0.0, 0.0, 1.0, 1.0);
        {
            vk::BufferPointer<uint> shapePtr = vk::BufferPointer<uint>(g_MeshRoot.shapeTableAddr);
            uint* shapes = shapePtr.getPtr();
            uint ms = min(mat, kBlockStateMax - 1u);
            int3 sMin = VoxelShapeMin(shapes, ms);
            int3 sMax = VoxelShapeMax(shapes, ms);
            float3 qb, qe1, qe2;
            VoxelFaceQuad(x, y, z, f, sMin, sMax, qb, qe1, qe2);

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

        bool uAlongT1 = abs(nrm.x) < 0.5;

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
                vv = o2;
            } else if (abs(t1.y) > 0.5) {
                vv = 1.0 - o1;
            } else {
                vv = 1.0 - o2;
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
