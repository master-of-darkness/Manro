struct PushConstants {
    float2 scale;
    float2 translate;
};

struct VSInput {
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(1)]] float2 uv  : TEXCOORD0;
    [[vk::location(2)]] float4 col : COLOR;
};

struct VSOutput {
    float4 pos : SV_Position;
    [[vk::location(0)]] float4 col : COLOR;
    [[vk::location(1)]] float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VSOutput VSMain(VSInput input, uniform PushConstants pc) {
    VSOutput output;
    output.pos = float4(input.pos * pc.scale + pc.translate, 0, 1);
    output.col = input.col;
    output.uv  = input.uv;
    return output;
}

[[vk::binding(0, 0)]]
Sampler2D sTexture;

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target {
    return input.col * sTexture.Sample(input.uv);
}
