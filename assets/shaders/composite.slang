#include "nvshaders/tonemap_functions.h.slang"

struct CompositePushConstants {
    float exposure;
    float gamma;
    int outputIsSRGB;
    float _pad;
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
    // Full screen triangle
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target {
    float4 color = sceneColor.Sample(input.uv);
    
    // TODO: use applyTonemap from nvshaders
    
    float3 result = color.rgb * pc.exposure;
    
    result = result / (result + float3(1.0));
    
    if (pc.outputIsSRGB == 0) {
        result = pow(result, float3(1.0 / pc.gamma));
    }
    
    return float4(result, color.a);
}
