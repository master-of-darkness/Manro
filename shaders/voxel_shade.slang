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

    return level / (4.0 - 3.0 * level);
}

float VanillaParabolic(float level) {
    float d = 2.0 * level - 1.0;
    return d * d;
}

float4 VoxelShadeBody(VoxelFragIn input, bool doDiscard) {
    vk::BufferPointer<VoxelFragParams> frp = vk::BufferPointer<VoxelFragParams>(g_FragRoot.fragParamsAddr);
    VoxelFragParams fr = frp.getPtr()[0];

    uint f = input.faceState & 0x7u;
    uint tile = input.tile;
    uint mflags = input.mflags;

    float4 albedo = g_Tiles.Sample(float3(input.uv, float(tile)));

    if (doDiscard && albedo.a < 0.5) {
        discard;
    }

    float3 nrm = kFaceNormal[f];
    bool leafDown = ((mflags & 32u) != 0u) && (nrm.y < -0.5);


    float faceShade = 1.0;
    if (abs(nrm.y) > 0.5) {
        faceShade = (nrm.y > 0.0) ? 1.0 : 0.5;
        if (leafDown)
            faceShade = 0.9;
    } else if (abs(nrm.x) > 0.5) {
        faceShade = 0.6;
    } else {
        faceShade = 0.8;
    }


    float sky = clamp(input.vLight.x, 0.0, 15.0);
    float blk = clamp(input.vLight.y, 0.0, 15.0);
    float ao = clamp(input.vLight.z, 0.0, 1.0);

    float skyLevel = sky / 15.0;
    float blockLevel = blk / 15.0;
    float skyBright = VanillaBrightness(skyLevel);
    float blockBright = VanillaBrightness(blockLevel);


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

[earlydepthstencil]
float4 PSMainOpaque(VoxelFragIn input) : SV_Target {
    return VoxelShadeBody(input, false);
}

float4 PSMainCutout(VoxelFragIn input) : SV_Target {
    return VoxelShadeBody(input, true);
}
