#include "nvshaders/tonemap_functions.h.slang"

struct CompositePushConstants {
    TonemapperData tm;
    float2 imageSize;
};

layout(push_constant) ConstantBuffer<CompositePushConstants> pc;
layout(binding = 0) Sampler2D sceneColor;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target {
    float4 color = sceneColor.Sample(input.uv);
    
    float2 pixel = input.uv * pc.imageSize;
    float3 result = applyTonemap(pc.tm, color.rgb, pixel, pc.imageSize);
    
    return float4(result, color.a);
}

