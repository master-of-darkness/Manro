struct GpuCullData {
    float3 center;
    float radius;
    uint instanceId;
    uint _pad[3];
};

struct GpuMeshInstance {
    float4x4 modelMatrix;
    float4 normalMatrix[3];
    uint materialIndex;
    uint firstVertex;
    uint firstIndex;
    uint indexCount;
    float3 center;
    float radius;
    uint flags;
    uint _pad[3];
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
    float4 cameraPos;
    uint instanceCount;
    float maxDrawDistance;
    uint _pad[2];
};

[[vk::binding(0, 0)]]
StructuredBuffer<GpuCullData> cullData;

[[vk::binding(1, 0)]]
RWStructuredBuffer<GpuDrawCommand> drawCommands;

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> drawCount;

[[vk::binding(3, 0)]]
StructuredBuffer<GpuMeshInstance> meshInstances;

[[vk::push_constant]]
CullPushConstants pc;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= pc.instanceCount) return;

    GpuCullData cull = cullData[dtid.x];
    GpuMeshInstance inst = meshInstances[cull.instanceId];
    
    float3 worldCenter = cull.center;
    float worldRadius = cull.radius;

    bool visible = true;

    float3 camToCenter = worldCenter - pc.cameraPos.xyz;
    float distSq = dot(camToCenter, camToCenter);
    float maxDist = pc.maxDrawDistance + worldRadius;
    if (distSq > maxDist * maxDist) {
        visible = false;
    }

    if (visible) {
        for (int i = 0; i < 6; ++i) {
            float4 plane = pc.planes[i];
            float dist = dot(plane.xyz, worldCenter) + plane.w;
            if (dist < -worldRadius) {
                visible = false;
                break;
            }
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
        cmd.firstInstance = cull.instanceId;
        
        drawCommands[drawIdx] = cmd;
    }
}
