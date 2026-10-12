import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_FragRoot;

[[vk::binding(0, 0)]]
Sampler2DArray g_Tiles;

struct VoxelFragIn {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint faceState : TEXCOORD1;
    nointerpolation uint tile : TEXCOORD2;
    nointerpolation uint mflags : TEXCOORD3;
    float3 vLight : TEXCOORD4;
};

float VanillaBrightness(float level) {
    // Exact vanilla lightmap curve: level in 0..1 (light/15).
    return level / (4.0 - 3.0 * level);
}

float VanillaParabolic(float level) {
    float d = 2.0 * level - 1.0;
    return d * d;
}

[earlydepthstencil]
float4 PSMain(VoxelFragIn input) : SV_Target {
    vk::BufferPointer<VoxelFragParams> frp = vk::BufferPointer<VoxelFragParams>(g_FragRoot.fragParamsAddr);
    VoxelFragParams fr = frp.getPtr()[0];

    uint f = input.faceState & 0x7u;
    uint tile = input.tile;
    uint mflags = input.mflags;

    float4 albedo = g_Tiles.Sample(float3(input.uv, float(tile)));

    if ((mflags & 2u) != 0u && albedo.a < 0.5) {
        discard;
    }

    float3 nrm = kFaceNormal[f];

    // Vanilla directional face shading (top 1.0, bottom 0.5, north/south 0.8, east/west 0.6).
    float faceShade = 1.0;
    if (abs(nrm.y) > 0.5) {
        faceShade = (nrm.y > 0.0) ? 1.0 : 0.5;
    } else if (abs(nrm.x) > 0.5) {
        faceShade = 0.6;
    } else {
        faceShade = 0.8;
    }

    // Interpolated smooth lighting from mesh (sky/block 0..15, ao brightness 0.5..1.0).
    float sky = clamp(input.vLight.x, 0.0, 15.0);
    float blk = clamp(input.vLight.y, 0.0, 15.0);
    float ao = clamp(input.vLight.z, 0.0, 1.0);

    float skyLevel = sky / 15.0;
    float blockLevel = blk / 15.0;
    float skyBright = VanillaBrightness(skyLevel);
    float blockBright = VanillaBrightness(blockLevel);

    // Vanilla day lightmap uniforms (Overworld, clear noon).
    float3 ambient = float3(0.02, 0.02, 0.02);
    float3 skyColor = float3(1.0, 1.0, 1.0);
    float3 blockTint = float3(1.0, 0.62, 0.32);
    float3 blockColor = lerp(blockTint, float3(1.0, 1.0, 1.0),
                             0.9 * VanillaParabolic(blockLevel));

    float3 lightColor = ambient + skyColor * skyBright + blockColor * blockBright;
    lightColor = clamp(lightColor, 0.0, 1.0);

    float3 lit = albedo.rgb * lightColor * faceShade * ao;

    if (fr.giEnabled != 0u) {
        vk::BufferPointer<float4> gi = vk::BufferPointer<float4>(fr.giAddr);
        lit += gi.getPtr()[0].rgb;
    }
    return float4(lit, 1.0);
}
