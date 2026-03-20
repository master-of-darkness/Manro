struct GpuCullData {
    float3 center;
    float radius;
    uint indexCount;
    uint firstIndex;
    uint firstVertex;
    uint instanceId;
};

struct GpuDrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct CullPushConstants {
    float4 planes[6];
    uint instanceCount;
};

[[vk::binding(0, 0)]]
StructuredBuffer<GpuCullData> cullData;

[[vk::binding(1, 0)]]
RWStructuredBuffer<GpuDrawCommand> drawCommands;

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> drawCount;

[[vk::push_constant]]
CullPushConstants pc;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= pc.instanceCount) return;

    GpuCullData inst = cullData[dtid.x];
    
    float3 worldCenter = inst.center;
    float worldRadius = inst.radius;

    bool visible = true;
    for (int i = 0; i < 6; ++i) {
        float4 plane = pc.planes[i];
        float dist = dot(plane.xyz, worldCenter) + plane.w;
        if (dist < -worldRadius) {
            visible = false;
            break;
        }
    }

    if (visible) {
        uint drawIdx;
        InterlockedAdd(drawCount[0], 1, drawIdx);
        
        GpuDrawCommand cmd;
        cmd.indexCount = inst.indexCount;
        cmd.instanceCount = 1;
        cmd.firstIndex = inst.firstIndex;
        cmd.vertexOffset = (int)inst.firstVertex;
        cmd.firstInstance = inst.instanceId;
        
        drawCommands[drawIdx] = cmd;
    }
}
