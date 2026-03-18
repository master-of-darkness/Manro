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

// ---------------------------------------------------------------------------
// Build a frustum plane through the camera origin from two view-space rays.
// The returned normal points INWARD (toward the frustum interior).
// ---------------------------------------------------------------------------
float4 makePlane(float3 a, float3 b)
{
    // Plane through origin: nx*x + ny*y + nz*z = 0  =>  w = 0
    float3 n = normalize(cross(a, b));
    return float4(n, 0.0);
}

// Signed distance of a point from a plane (w component is the plane d-term).
float signedDist(float4 plane, float3 p)
{
    return dot(plane.xyz, p) + plane.w;
}

// ---------------------------------------------------------------------------
// Per-tile compute shader – one thread per tile.
// ---------------------------------------------------------------------------
[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 gid : SV_DispatchThreadID)
{
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

    // -------------------------------------------------------------------------
    // 1. Compute tile screen-space bounds (clamp to screen edges for last tiles)
    // -------------------------------------------------------------------------
    float minPX = float(tx)       * tileSize;
    float maxPX = min(float(tx + 1) * tileSize, screenW);
    float minPY = float(ty)       * tileSize;
    float maxPY = min(float(ty + 1) * tileSize, screenH);

    // -------------------------------------------------------------------------
    // 2. Convert to NDC [-1, 1]
    // -------------------------------------------------------------------------
    float minNX = minPX / screenW * 2.0 - 1.0;
    float maxNX = maxPX / screenW * 2.0 - 1.0;
    float minNY = minPY / screenH * 2.0 - 1.0;
    float maxNY = maxPY / screenH * 2.0 - 1.0;

    // -------------------------------------------------------------------------
    // 3. Unproject NDC to view-space at z = -1 plane.
    //    For a standard perspective matrix: view_x = NDC_x / proj[0][0]
    //    (proj[1][1] is already negated in Renderer.cpp for Vulkan conventions)
    // -------------------------------------------------------------------------
    float p00 = pc.proj[0][0];   // 2*n / (r - l)
    float p11 = pc.proj[1][1];   // -2*n / (t - b)  (negated)

    // Four corner rays in view space (at z = -1 depth plane)
    float3 TL = float3(minNX / p00,  maxNY / p11, -1.0);  // top-left
    float3 TR = float3(maxNX / p00,  maxNY / p11, -1.0);  // top-right
    float3 BL = float3(minNX / p00,  minNY / p11, -1.0);  // bottom-left
    float3 BR = float3(maxNX / p00,  minNY / p11, -1.0);  // bottom-right

    // -------------------------------------------------------------------------
    // 4. Build 4 frustum planes through origin, normals pointing inward.
    //    Winding: cross(a, b) with a,b chosen so the normal faces the frustum interior.
    // -------------------------------------------------------------------------
    float4 planeLeft   = makePlane(BL, TL);  // left edge: BL->TL, normal points right (+x)
    float4 planeRight  = makePlane(TR, BR);  // right edge: TR->BR, normal points left (−x)
    float4 planeBottom = makePlane(BR, BL);  // bottom edge: BR->BL, normal points up (+y)
    float4 planeTop    = makePlane(TL, TR);  // top edge: TL->TR, normal points down (−y)

    // -------------------------------------------------------------------------
    // 5. Cull each light against the tile frustum.
    // -------------------------------------------------------------------------
    uint count = 0;

    for (uint i = 0; i < pc.lightCount && count < pc.maxPerTile; ++i)
    {
        GltfLight light = lights[i];

        // Directional lights are infinite – they affect every tile.
        if (light.type == int(LightType::eLightTypeDirectional))
        {
            tileLightIndices[offset + count] = i;
            count++;
            continue;
        }

        // ----- Positional light (point or spot) -----

        // Transform world-space position to view space.
        float3 viewPos = mul(pc.view, float4(light.position, 1.0)).xyz;

        // Influence radius from angularSizeOrInvRange (= 1/range for positional lights).
        // Guard against nearly-zero to avoid enormous radii.
        float invRange = light.angularSizeOrInvRange;
        float radius   = (invRange > 1e-4) ? (1.0 / invRange) : 1000.0;

        // --- Z-range test (view space: camera looks toward -Z) ---
        // The sphere must overlap the [−far, −near] range on the Z axis.
        if (viewPos.z + radius < -far)  continue;
        if (viewPos.z - radius > -near) continue;

        // --- Tile frustum plane tests ---
        // A sphere is outside a plane when the signed distance of its centre
        // is more negative than its radius (i.e. entirely in the "outside" half-space).
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
