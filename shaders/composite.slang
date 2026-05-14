#include "nvshaders/tonemap_functions.h.slang"

struct CompositePushConstants {
    TonemapperData tm;
    float2 imageSize;
        float bloomIntensity;
    float bloomThreshold;
    int bloomEnabled;
    int _pad;
};

layout(push_constant) ConstantBuffer<CompositePushConstants> pc;
layout(binding = 0) Sampler2D sceneColor;
layout(binding = 1) StructuredBuffer<float> autoExposureLuminance;

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
    float3 hdrColor = color.rgb;

    if (pc.bloomEnabled != 0) {
        float3 bloom = max(hdrColor - pc.bloomThreshold.xxx, 0.0) * pc.bloomIntensity;
        hdrColor += bloom;
    }

    if (pc.tm.autoExposure == 1) {
        const float middleGrey = 0.18f;
        const float targetLuminance = max(0.001f, autoExposureLuminance[0]);
        hdrColor *= middleGrey / targetLuminance;
    }
    
    float2 pixel = input.uv * pc.imageSize;
    float3 result = applyTonemap(pc.tm, hdrColor, pixel, pc.imageSize);
    
    return float4(result, color.a);
}
