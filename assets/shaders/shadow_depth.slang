#include "nvshaders/gltf_scene_io.h.slang"

struct ShadowPushConstants {
    float4x4 lightViewProj;
};

struct ShadowData {
    float4x4 lightViewProj;
    float4   lightDir;
    float2   shadowMapSize;
    float    normalBias;
    float    _pad;
};

layout(set = 0, binding = 17) ConstantBuffer<ShadowData> shadowData;

struct VSInput {
    [[vk::location(0)]] float3 position     : POSITION;
    [[vk::location(1)]] float3 normal       : NORMAL;

    [[vk::location(4)]]  float4 modelRow0   : INSTANCE_MODEL0;
    [[vk::location(5)]]  float4 modelRow1   : INSTANCE_MODEL1;
    [[vk::location(6)]]  float4 modelRow2   : INSTANCE_MODEL2;
    [[vk::location(7)]]  float4 modelRow3   : INSTANCE_MODEL3;
    [[vk::location(8)]]  float4 normalRow0  : INSTANCE_NORMAL0;
    [[vk::location(9)]]  float4 normalRow1  : INSTANCE_NORMAL1;
    [[vk::location(10)]] float4 normalRow2  : INSTANCE_NORMAL2;
};

[[vk::push_constant]]
ShadowPushConstants pc;

[shader("vertex")]
float4 VSMain(VSInput input) : SV_Position {
    float4x4 model = float4x4(
        input.modelRow0,
        input.modelRow1,
        input.modelRow2,
        input.modelRow3
    );

    float3 worldNormal = normalize(mul((float3x3)float3x3(input.normalRow0.xyz, input.normalRow1.xyz, input.normalRow2.xyz), input.normal.xyz));
    float3 worldPos = mul(model, float4(input.position, 1.0)).xyz;

    float3 biasedPos = worldPos + worldNormal * shadowData.normalBias;

    return mul(pc.lightViewProj, float4(biasedPos, 1.0));
}