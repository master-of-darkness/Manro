#include "nvshaders/gltf_scene_io.h.slang"

struct UniformBufferObject {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    float4 camPos;
};

layout(set = 0, binding = 0) ConstantBuffer<UniformBufferObject> ubo;
layout(set = 0, binding = 1) SamplerCube skyboxTexture;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 uvw      : TEXCOORD0;
};

[shader("vertex")]
VSOutput VSMain(VSInput input) {
    VSOutput output;

    float3x3 viewRotation = (float3x3)ubo.view;
    float4 viewPos = float4(mul(viewRotation, input.position), 1.0);
    float4 pos = mul(ubo.proj, viewPos);

    output.position = pos.xyww;
    output.uvw = input.position;

    return output;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target {
    return skyboxTexture.Sample(input.uvw);
}
