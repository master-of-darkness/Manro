#include "nvshaders/gltf_scene_io.h.slang"

struct CullPushConstants {
    float4x4 view;
    float4x4 proj;
    float4   screenTile;  // x,y = screen dims, z,w = tile size
    uint     lightCount;
    uint     maxPerTile;
    uint     tilesX;
    uint     tilesY;
    float4   zParams;     // x = near, y = far
};

struct TileHeader {
    uint offset;
    uint count;
    uint pad0;
    uint pad1;
};

layout(push_constant) ConstantBuffer<CullPushConstants> pc;
layout(binding = 0) StructuredBuffer<GltfLight>         lights;
layout(binding = 1) RWStructuredBuffer<TileHeader>       tileHeaders;
layout(binding = 2) RWStructuredBuffer<uint>             tileLightIndices;

float4 makePlane(float3 a, float3 b)
{
    float3 n = normalize(cross(a, b));
    return float4(n, 0.0);
}

float signedDist(float4 plane, float3 p)
{
    return dot(plane.xyz, p) + plane.w;
}

groupshared float3 shared_viewPos[1024];
groupshared float  shared_radius[1024];

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 gid : SV_DispatchThreadID, uint localIdx : SV_GroupIndex)
{
    uint lightCount = pc.lightCount;
    for (uint i = localIdx; i < lightCount; i += 256)
    {
        GltfLight light = lights[i];
        if (light.type != int(LightType::eLightTypeDirectional)) {
            shared_viewPos[i] = mul(pc.view, float4(light.position, 1.0)).xyz;
            float invRange = light.angularSizeOrInvRange;
            shared_radius[i] = (invRange > 1e-4) ? (1.0 / invRange) : 1000.0;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint tx = gid.x;
    uint ty = gid.y;
    if (tx >= pc.tilesX || ty >= pc.tilesY)
        return;

    uint tileIdx = ty * pc.tilesX + tx;
    uint offset  = tileIdx * pc.maxPerTile;

    float screenW  = pc.screenTile.x;
    float screenH  = pc.screenTile.y;
    float tileSize = pc.screenTile.z;
    float near     = pc.zParams.x;
    float far      = pc.zParams.y;

    float minPX = float(tx)       * tileSize;
    float maxPX = min(float(tx + 1) * tileSize, screenW);
    float minPY = float(ty)       * tileSize;
    float maxPY = min(float(ty + 1) * tileSize, screenH);

    float minNX = minPX / screenW * 2.0 - 1.0;
    float maxNX = maxPX / screenW * 2.0 - 1.0;
    float minNY = minPY / screenH * 2.0 - 1.0;
    float maxNY = maxPY / screenH * 2.0 - 1.0;

    float p00 = pc.proj[0][0];   // 2*n / (r - l)
    float p11 = pc.proj[1][1];   // -2*n / (t - b)  (negated)

    float3 TL = float3(minNX / p00,  maxNY / p11, -1.0);  // top-left
    float3 TR = float3(maxNX / p00,  maxNY / p11, -1.0);  // top-right
    float3 BL = float3(minNX / p00,  minNY / p11, -1.0);  // bottom-left
    float3 BR = float3(maxNX / p00,  minNY / p11, -1.0);  // bottom-right

    float4 planeLeft   = makePlane(BL, TL);  // left edge: BL->TL, normal points right (+x)
    float4 planeRight  = makePlane(TR, BR);  // right edge: TR->BR, normal points left (−x)
    float4 planeBottom = makePlane(BR, BL);  // bottom edge: BR->BL, normal points up (+y)
    float4 planeTop    = makePlane(TL, TR);  // top edge: TL->TR, normal points down (−y)

    uint count = 0;

    for (uint i = 0; i < pc.lightCount && count < pc.maxPerTile; ++i)
    {
        GltfLight light = lights[i];

        if (light.type == int(LightType::eLightTypeDirectional))
        {
            tileLightIndices[offset + count] = i;
            count++;
            continue;
        }

        float3 viewPos = shared_viewPos[i];
        float radius   = shared_radius[i];

        if (viewPos.z + radius < -far)  continue;
        if (viewPos.z - radius > -near) continue;

        if (signedDist(planeLeft,   viewPos) + radius < 0.0) continue;
        if (signedDist(planeRight,  viewPos) + radius < 0.0) continue;
        if (signedDist(planeBottom, viewPos) + radius < 0.0) continue;
        if (signedDist(planeTop,    viewPos) + radius < 0.0) continue;

        tileLightIndices[offset + count] = i;
        count++;
    }

    tileHeaders[tileIdx].offset = offset;
    tileHeaders[tileIdx].count  = count;
}
