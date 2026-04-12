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
    uint enableFrustumCulling;
    uint _pad;
};

[[vk::binding(0, 0)]]
StructuredBuffer<GpuCullData> cullData;

[[vk::binding(1, 0)]]
RWStructuredBuffer<GpuDrawCommand> drawCommands;

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> drawCount;

[[vk::binding(3, 0)]]
RWStructuredBuffer<GpuMeshInstance> meshInstances;

[[vk::push_constant]]
CullPushConstants pc;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= pc.instanceCount) return;

    GpuCullData cull = cullData[dtid.x];
    GpuMeshInstance inst = meshInstances[cull.instanceId];

    // Compute normal matrix transpose(inverse(mat3(modelMatrix)))
    float3x3 M = float3x3(inst.modelMatrix[0].xyz, inst.modelMatrix[1].xyz, inst.modelMatrix[2].xyz);
    // determinant
    float det = M[0][0] * (M[1][1] * M[2][2] - M[2][1] * M[1][2])
              - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
              + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
    float invDet = (abs(det) > 1e-8f) ? (1.0f / det) : 0.0f;
    // cofactor matrix
    float3x3 N;
    N[0][0] =  (M[1][1] * M[2][2] - M[2][1] * M[1][2]) * invDet;
    N[0][1] = -(M[0][1] * M[2][2] - M[0][2] * M[2][1]) * invDet;
    N[0][2] =  (M[0][1] * M[1][2] - M[0][2] * M[1][1]) * invDet;
    N[1][0] = -(M[1][0] * M[2][2] - M[1][2] * M[2][0]) * invDet;
    N[1][1] =  (M[0][0] * M[2][2] - M[0][2] * M[2][0]) * invDet;
    N[1][2] = -(M[0][0] * M[1][2] - M[1][0] * M[0][2]) * invDet;
    N[2][0] =  (M[1][0] * M[2][1] - M[2][0] * M[1][1]) * invDet;
    N[2][1] = -(M[0][0] * M[2][1] - M[2][0] * M[0][1]) * invDet;
    N[2][2] =  (M[0][0] * M[1][1] - M[1][0] * M[0][1]) * invDet;
    // transpose(inverse(M)) = transpose of cofactor/det, which is N transposed
    meshInstances[cull.instanceId].normalMatrix[0] = float4(N[0][0], N[1][0], N[2][0], 0);
    meshInstances[cull.instanceId].normalMatrix[1] = float4(N[0][1], N[1][1], N[2][1], 0);
    meshInstances[cull.instanceId].normalMatrix[2] = float4(N[0][2], N[1][2], N[2][2], 0);

    float3 worldCenter = cull.center;
    float worldRadius = cull.radius;

    bool visible = true;

    float3 camToCenter = worldCenter - pc.cameraPos.xyz;
    float distSq = dot(camToCenter, camToCenter);
    float maxDist = pc.maxDrawDistance + worldRadius;
    if (distSq > maxDist * maxDist) {
        visible = false;
    }

    if (visible && pc.enableFrustumCulling != 0) {
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
