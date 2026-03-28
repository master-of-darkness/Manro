struct PushConstants {
    float4x4 viewProj;
};

[[vk::push_constant]]
PushConstants pc;

struct VertexIn {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] uint   color    : COLOR0;
};

struct VertexOut {
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

[shader("vertex")]
VertexOut VSMain(VertexIn vin) {
    VertexOut vout;
    vout.sv_position = mul(pc.viewProj, float4(vin.position, 1.0));
    float r = float((vin.color)       & 0xFF) / 255.0;
    float g = float((vin.color >>  8) & 0xFF) / 255.0;
    float b = float((vin.color >> 16) & 0xFF) / 255.0;
    float a = float((vin.color >> 24) & 0xFF) / 255.0;
    vout.color = float4(r, g, b, a);
    return vout;
}

[shader("fragment")]
float4 PSMain(VertexOut fin) : SV_Target {
    return fin.color;
}
