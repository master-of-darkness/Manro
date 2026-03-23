#include "nvshaders/gltf_scene_io.h.slang"
#include "nvshaders/pbr_material_types.h.slang"
#include "nvshaders/bsdf_functions.h.slang"
#include "nvshaders/light_contrib.h.slang"

struct UniformBufferObject {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    float4 camPos;
    float exposure;
    float gamma;
    float prefilteredCubeMipLevels;
    float scaleIBLAmbient;
    int lightCount;
    int padding0;
    float padding1;
    float padding2;
    float2 screenDimensions;
    float nearZ;
    float farZ;
    float slicesZ;
    float _pad3;
    float4x4 reflectionVP;
    int reflectionEnabled;
    int reflectionPass;
    float2 _reflectPad0;
    float4 clipPlaneWS;
    float reflectionIntensity;
    int enableRayQueryReflections;
    int enableRayQueryTransparency;
    float _padReflect[1];
    int geometryInfoCount;
    int _padGeo[3];
    float4 _rqReservedWorldPos;
    int materialCount;
    int _padMat[3];
};

struct ShadowData {
    float4x4 lightViewProj;
    float4   lightDir;
    float2   shadowMapSize;
    float    normalBias;
    float    _pad;
};

// TileHeader (16 bytes)
struct TileHeader { uint offset; uint count; uint pad0; uint pad1; };

// Bindings
layout(set = 0, binding = 0)  ConstantBuffer<UniformBufferObject> ubo;
layout(set = 0, binding = 1)  SamplerState defaultSampler;
layout(set = 1, binding = 0)  Texture2D bindlessTextures[1024];
layout(set = 0, binding = 6)  StructuredBuffer<GltfLight>          lights;
layout(set = 0, binding = 7)  StructuredBuffer<TileHeader>         tileHeaders;
layout(set = 0, binding = 8)  StructuredBuffer<uint>               tileLightIndices;
layout(set = 0, binding = 13) StructuredBuffer<GltfShadeMaterial>  materials;
layout(set = 0, binding = 14) StructuredBuffer<GltfTextureInfo>    texInfos;

layout(set = 0, binding = 15) Texture2D<float>  shadowMap;
layout(set = 0, binding = 16) SamplerComparisonState shadowSampler;

layout(set = 0, binding = 17) ConstantBuffer<ShadowData> shadowData;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
    [[vk::location(2)]] float2 uv       : TEXCOORD0;
    [[vk::location(3)]] float4 tangent  : TANGENT;

    // Instanced
    [[vk::location(4)]]  float4x4 modelMatrix : INSTANCE_MODEL;
    [[vk::location(8)]]  float4   normalRow0  : INSTANCE_NORMAL0;
    [[vk::location(9)]]  float4   normalRow1  : INSTANCE_NORMAL1;
    [[vk::location(10)]] float4   normalRow2  : INSTANCE_NORMAL2;
    [[vk::location(11)]] uint     materialIndex : INSTANCE_MAT_INDEX;
};

struct VSOutput {
    float4 position      : SV_Position;
    [[vk::location(0)]] float3 worldPos  : POSITION;
    [[vk::location(1)]] float3 normal    : NORMAL;
    [[vk::location(2)]] float2 uv        : TEXCOORD0;
    [[vk::location(3)]] float3 tangent   : TANGENT;
    [[vk::location(4)]] float3 bitangent : BITANGENT;
    [[vk::location(5)]] nointerpolation uint materialIndex : MAT_INDEX;
    [[vk::location(6)]] float4 shadowPos : SHADOW_POS;
};

[shader("vertex")]
VSOutput VSMain(VSInput input) {
    VSOutput output;

    float4 worldPos4 = mul(input.modelMatrix, float4(input.position, 1.0));
    output.worldPos  = worldPos4.xyz;
    output.position  = mul(ubo.proj, mul(ubo.view, worldPos4));

    output.uv = input.uv;

    float3x3 nMat = float3x3(input.normalRow0.xyz, input.normalRow1.xyz, input.normalRow2.xyz);
    output.normal    = normalize(mul(nMat, input.normal));
    output.tangent   = normalize(mul(nMat, input.tangent.xyz));
    output.bitangent = cross(output.normal, output.tangent) * input.tangent.w;

    output.materialIndex = input.materialIndex;

    float3 biasedWorld = worldPos4.xyz + output.normal * shadowData.normalBias;
    output.shadowPos   = mul(shadowData.lightViewProj, float4(biasedWorld, 1.0));

    return output;
}

float2 ShadowUV(float4 shadowPos) {
    float3 proj = shadowPos.xyz / shadowPos.w;
    return proj.xy * 0.5 + 0.5;
}

float SampleShadowPCF(float4 shadowPos) {
    float3 proj   = shadowPos.xyz / shadowPos.w;
    float2 uv     = proj.xy * 0.5 + 0.5;
    float  depth  = proj.z - shadowData.lightDir.w;

    if (any(uv < 0.0) || any(uv > 1.0) || depth < 0.0 || depth > 1.0) return 1.0;

    float2 texelSize = 1.0 / shadowData.shadowMapSize;
    float shadow = 0.0;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            float2 offset = float2(dx, dy) * texelSize;
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + offset, depth);
        }
    }
    return shadow / 9.0;
}

PbrMaterial localEvaluateMaterial(GltfShadeMaterial material, float3 N, float3 T, float3 B, float2 uv) {
    PbrMaterial pbrMat = defaultPbrMaterial();
    pbrMat.baseColor = material.pbrBaseColorFactor.rgb;
    pbrMat.opacity   = material.pbrBaseColorFactor.a;

    if (material.pbrBaseColorTexture > 0) {
        GltfTextureInfo tinfo = texInfos[NonUniformResourceIndex(uint(material.pbrBaseColorTexture))];
        if (tinfo.index >= 0) {
            float2 tt = mul(float3(uv, 1.0), tinfo.uvTransform).xy;
            float4 baseSample = bindlessTextures[NonUniformResourceIndex(uint(tinfo.index))].Sample(defaultSampler, tt);
            pbrMat.baseColor *= baseSample.rgb;
            pbrMat.opacity   *= baseSample.a;
        }
    }

    float roughness = material.pbrRoughnessFactor;
    float metallic  = material.pbrMetallicFactor;
    if (material.pbrMetallicRoughnessTexture > 0) {
        GltfTextureInfo tinfo = texInfos[NonUniformResourceIndex(uint(material.pbrMetallicRoughnessTexture))];
        if (tinfo.index >= 0) {
            float2 tt    = mul(float3(uv, 1.0), tinfo.uvTransform).xy;
            float4 mrSample = bindlessTextures[NonUniformResourceIndex(uint(tinfo.index))].Sample(defaultSampler, tt);
            roughness *= mrSample.g;
            metallic  *= mrSample.b;
        }
    }

    pbrMat.roughness = float2(roughness * roughness);
    pbrMat.metallic  = metallic;
    pbrMat.emissive  = material.emissiveFactor;

    if (material.emissiveTexture > 0) {
        GltfTextureInfo tinfo = texInfos[NonUniformResourceIndex(uint(material.emissiveTexture))];
        if (tinfo.index >= 0) {
            float2 tt = mul(float3(uv, 1.0), tinfo.uvTransform).xy;
            pbrMat.emissive *= bindlessTextures[NonUniformResourceIndex(uint(tinfo.index))].Sample(defaultSampler, tt).rgb;
        }
    }

    pbrMat.N = N;
    if (material.normalTexture > 0) {
        GltfTextureInfo tinfo = texInfos[NonUniformResourceIndex(uint(material.normalTexture))];
        if (tinfo.index >= 0) {
            float2 tt           = mul(float3(uv, 1.0), tinfo.uvTransform).xy;
            float3 normalSample = bindlessTextures[NonUniformResourceIndex(uint(tinfo.index))].Sample(defaultSampler, tt).xyz * 2.0 - 1.0;
            float3x3 tbn        = float3x3(T, B, N);
            pbrMat.N            = normalize(mul(tbn, normalSample));
        }
    }

    return pbrMat;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target {
    GltfShadeMaterial mat  = materials[input.materialIndex];
    float3 pbrN = normalize(input.normal);
    float3 pbrT = normalize(input.tangent);
    float3 pbrB = normalize(input.bitangent);

    float2 uv = float2(input.uv.x, 1.0 - input.uv.y);
    PbrMaterial pbrMat = localEvaluateMaterial(mat, pbrN, pbrT, pbrB, uv);

    float3 V      = normalize(ubo.camPos.xyz - input.worldPos);
    float3 result = float3(0.0);

    uint tileSize = 16u;
    uint tileX    = uint(input.position.x) / tileSize;
    uint tileY    = uint(input.position.y) / tileSize;
    uint tilesX   = uint(ubo.screenDimensions.x + float(tileSize) - 1.0) / tileSize;
    uint tileIdx  = tileY * tilesX + tileX;

    uint2 hdr        = uint2(tileHeaders[tileIdx].offset, tileHeaders[tileIdx].count);
    uint  tileOffset = hdr.x;
    uint  tileCount  = hdr.y;

    float shadowFactor = SampleShadowPCF(input.shadowPos);

    for (uint ti = 0; ti < tileCount; ++ti) {
        uint      i     = tileLightIndices[tileOffset + ti];
        GltfLight light = lights[i];

        LightContrib contrib = singleLightContribution(light, input.worldPos, pbrMat.N);

        if (all(contrib.intensity <= 0.0)) continue;

        float3 L = -contrib.incidentVector;

        if (light.type == int(LightType::eLightTypeDirectional)) {
            contrib.intensity *= shadowFactor;
        }

        BsdfEvaluateData evalData;
        evalData.k1 = V;
        evalData.k2 = L;
        bsdfEvaluateSimple(evalData, pbrMat);

        result += (evalData.bsdf_diffuse + evalData.bsdf_glossy) * contrib.intensity;
    }

    result += pbrMat.baseColor * 0.1 + pbrMat.emissive;

    return float4(result, 1.0);
}