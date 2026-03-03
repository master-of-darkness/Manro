// cube.slang — position + vertex-colour + UV, with texture sampling.
// When no real texture is bound, the white 1×1 fallback ensures vertex colours
// come through unchanged (white * vertexColor = vertexColor).

struct VSInput {
    float3 position : POSITION;
    float3 color    : COLOR0;
    float2 uv       : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR0;
    float2 uv       : TEXCOORD0;
};

// Push-constant block: MVP matrix + RGB tint + padding to satisfy 16-byte alignment
[[vk::push_constant]]
cbuffer PushConstants {
    float4x4 mvp;
    float3   tint;
    float    _pad;
};

// Texture + sampler at set=0, binding=0
[[vk::binding(0, 0)]]
Sampler2D albedoTexture;

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    output.position = mul(mvp, float4(input.position, 1.0));
    // Multiply vertex colour by the per-draw tint so props can be recoloured
    // at runtime without touching mesh data. White tint (1,1,1) = no change.
    output.color    = input.color * tint;
    output.uv       = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    float4 texColor = albedoTexture.Sample(input.uv);
    // Modulate texture by vertex colour so untextured meshes still show
    // their diffuse colour (texColor == white when the 1x1 white fallback is bound)
    return float4(texColor.rgb * input.color, texColor.a);
}