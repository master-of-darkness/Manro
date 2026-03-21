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

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
    [[vk::location(2)]] float2 uv       : TEXCOORD0;
    [[vk::location(3)]] float4 tangent  : TANGENT;
    
    // Instanced
    [[vk::location(4)]] float4x4 modelMatrix : INSTANCE_MODEL;
    [[vk::location(8)]] float4 normalRow0 : INSTANCE_NORMAL0;
    [[vk::location(9)]] float4 normalRow1 : INSTANCE_NORMAL1;
    [[vk::location(10)]] float4 normalRow2 : INSTANCE_NORMAL2;
    [[vk::location(11)]] uint materialIndex : INSTANCE_MAT_INDEX;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldPos : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float3 tangent : TANGENT;
    [[vk::location(4)]] float3 bitangent : BITANGENT;
    [[vk::location(5)]] nointerpolation uint materialIndex : MAT_INDEX;
};

[shader("vertex")]
VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    float4 worldPos = mul(input.modelMatrix, float4(input.position, 1.0));
    output.worldPos = worldPos.xyz;
    output.position = mul(ubo.proj, mul(ubo.view, worldPos));
    
    output.uv = input.uv;
    
    float3x3 nMat = float3x3(input.normalRow0.xyz, input.normalRow1.xyz, input.normalRow2.xyz);
    output.normal = normalize(mul(nMat, input.normal));
    output.tangent = normalize(mul(nMat, input.tangent.xyz));
    output.bitangent = cross(output.normal, output.tangent) * input.tangent.w;
    
    output.materialIndex = input.materialIndex;
    
    return output;
}

PbrMaterial localEvaluateMaterial(GltfShadeMaterial material, float3 N, float3 T, float3 B, float2 uv) {
    PbrMaterial pbrMat = defaultPbrMaterial();
    pbrMat.baseColor = material.pbrBaseColorFactor.rgb;
    pbrMat.opacity = material.pbrBaseColorFactor.a;
    
    if (material.pbrBaseColorTexture > 0) {
        GltfTextureInfo tinfo = texInfos[NonUniformResourceIndex(uint(material.pbrBaseColorTexture))];
        if (tinfo.index >= 0) {
            float2 tt = uv;
            tt = mul(float3(uv, 1.0), tinfo.uvTransform).xy;
            float4 baseSample = bindlessTextures[NonUniformResourceIndex(uint(tinfo.index))].Sample(defaultSampler, tt);
            pbrMat.baseColor *= baseSample.rgb;
            pbrMat.opacity *= baseSample.a;
        }
    }
    
    float roughness = material.pbrRoughnessFactor;
    float metallic = material.pbrMetallicFactor;
    if (material.pbrMetallicRoughnessTexture > 0) {
        GltfTextureInfo tinfo = texInfos[NonUniformResourceIndex(uint(material.pbrMetallicRoughnessTexture))];
        if (tinfo.index >= 0) {
            float2 tt = mul(float3(uv, 1.0), tinfo.uvTransform).xy;
            float4 mrSample = bindlessTextures[NonUniformResourceIndex(uint(tinfo.index))].Sample(defaultSampler, tt);
            roughness *= mrSample.g;
            metallic *= mrSample.b;
        }
    }
    
    pbrMat.roughness = float2(roughness * roughness);
    pbrMat.metallic = metallic;
    pbrMat.emissive = material.emissiveFactor;
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
            float2 tt = mul(float3(uv, 1.0), tinfo.uvTransform).xy;
            float3 normalSample = bindlessTextures[NonUniformResourceIndex(uint(tinfo.index))].Sample(defaultSampler, tt).xyz * 2.0 - 1.0;
            float3x3 tbn = float3x3(T, B, N);
            pbrMat.N = normalize(mul(tbn, normalSample));
        }
    }
    
    return pbrMat;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target {
    GltfShadeMaterial mat = materials[input.materialIndex];
    float3 pbrN = normalize(input.normal);
    float3 pbrT = normalize(input.tangent);
    float3 pbrB = normalize(input.bitangent);
    
    // UV flip
    float2 uv = float2(input.uv.x, 1.0 - input.uv.y);
    PbrMaterial pbrMat = localEvaluateMaterial(mat, pbrN, pbrT, pbrB, uv);
    
    float3 V = normalize(ubo.camPos.xyz - input.worldPos);
    float3 result = float3(0);

    uint tileSize = 16u;
    uint tileX    = uint(input.position.x) / tileSize;
    uint tileY    = uint(input.position.y) / tileSize;
    uint tilesX   = uint(ubo.screenDimensions.x + float(tileSize) - 1.0) / tileSize;
    uint tileIdx  = tileY * tilesX + tileX;

    uint2 hdr        = uint2(tileHeaders[tileIdx].offset, tileHeaders[tileIdx].count);
    uint  tileOffset = hdr.x;
    uint  tileCount  = hdr.y;

    for (uint ti = 0; ti < tileCount; ++ti) {
        uint i = tileLightIndices[tileOffset + ti];
        GltfLight light = lights[i];
        float3 L;
        float3 lightIntensity;

        if (light.type == int(LightType::eLightTypeDirectional)) {
            L = normalize(-light.direction);
            lightIntensity = light.color * light.intensity;
        } else {
            L = normalize(light.position - input.worldPos);
            float dist = length(light.position - input.worldPos);
            float attenuation = 1.0 / max(dist * dist, 0.001);
            lightIntensity = light.color * light.intensity * attenuation;
        }

        float cosTheta = max(0.0, dot(pbrMat.N, L));
        if (cosTheta > 0.0) {
            result += pbrMat.baseColor * lightIntensity * cosTheta;
        }
    }

    // Ambient fallback
    result += pbrMat.baseColor * 0.15;
    result += pbrMat.emissive;

    return float4(result, 1.0);
}
