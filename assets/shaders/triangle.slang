struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR;
};

[shader("vertex")]
VertexOut vertexMain(uint vertexID : SV_VertexID) {
    VertexOut output;
    
    // Hardcoded triangle
    float2 positions[3] = {
        float2(0.0, -0.5),
        float2(0.5, 0.5),
        float2(-0.5, 0.5)
    };
    
    float3 colors[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0)
    };
    
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.color = colors[vertexID];
    
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    return float4(input.color, 1.0);
}
