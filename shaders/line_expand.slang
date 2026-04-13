// GPU-driven line expansion compute shader
// Expands primitive commands (lines, boxes, spheres, etc.) into line vertices

struct DrawLineCmd {
    float3 start;
    uint color;
    float3 end;
    uint depthTest;
};

struct DrawBoxCmd {
    float3 center;
    uint color;
    float3 halfExtents;
    uint depthTest;
    float4x4 transform;
};

struct DrawSphereCmd {
    float3 center;
    float radius;
    uint color;
    uint segments;
    uint depthTest;
    uint _pad0;
};

struct DrawFrustumCmd {
    float4x4 invViewProj;
    uint color;
    uint depthTest;
    uint _pad0;
    uint _pad1;
};

struct DrawCrossCmd {
    float3 center;
    float size;
    uint color;
    uint depthTest;
};

struct LineVertex {
    float3 position;
    uint color;
};

struct DrawIndirectCmd {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

struct DrawCounts {
    uint lineCount;
    uint boxCount;
    uint sphereCount;
    uint frustumCount;
    uint crossCount;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<DrawLineCmd> lineCommands;
[[vk::binding(1, 0)]] RWStructuredBuffer<DrawBoxCmd> boxCommands;
[[vk::binding(2, 0)]] RWStructuredBuffer<DrawSphereCmd> sphereCommands;
[[vk::binding(3, 0)]] RWStructuredBuffer<DrawFrustumCmd> frustumCommands;
[[vk::binding(4, 0)]] RWStructuredBuffer<DrawCrossCmd> crossCommands;

[[vk::binding(5, 0)]] RWStructuredBuffer<LineVertex> verticesDepth;
[[vk::binding(6, 0)]] RWStructuredBuffer<LineVertex> verticesNoDepth;
[[vk::binding(7, 0)]] RWStructuredBuffer<DrawIndirectCmd> indirectDepth;
[[vk::binding(8, 0)]] RWStructuredBuffer<DrawIndirectCmd> indirectNoDepth;
[[vk::binding(9, 0)]] RWStructuredBuffer<uint> counters;

[[vk::push_constant]] DrawCounts counts;

static const float PI = 3.14159265359;
static const uint SPHERE_SEGMENTS = 8;

uint AllocateVertices(uint count, bool depthTest) {
    uint counterIdx = depthTest ? 0 : 1;
    uint baseIndex;
    InterlockedAdd(counters[counterIdx], count, baseIndex);
    return baseIndex;
}

void EmitLine(float3 a, float3 b, uint color, bool depthTest) {
    uint baseIdx = AllocateVertices(2, depthTest);
    
    if (depthTest) {
        verticesDepth[baseIdx].position = a;
        verticesDepth[baseIdx].color = color;
        verticesDepth[baseIdx + 1].position = b;
        verticesDepth[baseIdx + 1].color = color;
    } else {
        verticesNoDepth[baseIdx].position = a;
        verticesNoDepth[baseIdx].color = color;
        verticesNoDepth[baseIdx + 1].position = b;
        verticesNoDepth[baseIdx + 1].color = color;
    }
}

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint tid = threadId.x;
    
    // Process lines
    if (tid < counts.lineCount) {
        DrawLineCmd cmd = lineCommands[tid];
        EmitLine(cmd.start, cmd.end, cmd.color, cmd.depthTest != 0);
    }
    
    // Process boxes (12 edges)
    if (tid < counts.boxCount) {
        DrawBoxCmd cmd = boxCommands[tid];
        bool depthTest = cmd.depthTest != 0;
        
        float3 corners[8];
        float3 h = cmd.halfExtents;
        float3 localCorners[8] = {
            float3(-h.x, -h.y, -h.z),
            float3( h.x, -h.y, -h.z),
            float3( h.x,  h.y, -h.z),
            float3(-h.x,  h.y, -h.z),
            float3(-h.x, -h.y,  h.z),
            float3( h.x, -h.y,  h.z),
            float3( h.x,  h.y,  h.z),
            float3(-h.x,  h.y,  h.z),
        };
        
        for (int i = 0; i < 8; i++) {
            float4 worldPos = mul(cmd.transform, float4(cmd.center + localCorners[i], 1.0));
            corners[i] = worldPos.xyz;
        }
        
        // 12 edges
        int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, // bottom
            {4, 5}, {5, 6}, {6, 7}, {7, 4}, // top
            {0, 4}, {1, 5}, {2, 6}, {3, 7}  // verticals
        };
        
        for (int e = 0; e < 12; e++) {
            EmitLine(corners[edges[e][0]], corners[edges[e][1]], cmd.color, depthTest);
        }
    }
    
    // Process spheres (3 circles, 8 segments each = 24 lines)
    if (tid < counts.sphereCount) {
        DrawSphereCmd cmd = sphereCommands[tid];
        bool depthTest = cmd.depthTest != 0;
        uint segs = SPHERE_SEGMENTS;
        float step = 2.0 * PI / float(segs);
        
        for (uint i = 0; i < segs; i++) {
            float a0 = step * float(i);
            float a1 = step * float(i + 1);
            float c0 = cos(a0), s0 = sin(a0);
            float c1 = cos(a1), s1 = sin(a1);
            
            // XY circle
            float3 p0_xy = cmd.center + float3(c0, s0, 0.0) * cmd.radius;
            float3 p1_xy = cmd.center + float3(c1, s1, 0.0) * cmd.radius;
            EmitLine(p0_xy, p1_xy, cmd.color, depthTest);
            
            // XZ circle
            float3 p0_xz = cmd.center + float3(c0, 0.0, s0) * cmd.radius;
            float3 p1_xz = cmd.center + float3(c1, 0.0, s1) * cmd.radius;
            EmitLine(p0_xz, p1_xz, cmd.color, depthTest);
            
            // YZ circle
            float3 p0_yz = cmd.center + float3(0.0, c0, s0) * cmd.radius;
            float3 p1_yz = cmd.center + float3(0.0, c1, s1) * cmd.radius;
            EmitLine(p0_yz, p1_yz, cmd.color, depthTest);
        }
    }
    
    // Process frustums (12 edges)
    if (tid < counts.frustumCount) {
        DrawFrustumCmd cmd = frustumCommands[tid];
        bool depthTest = cmd.depthTest != 0;
        
        float4 ndc[8] = {
            float4(-1, -1, -1, 1),
            float4( 1, -1, -1, 1),
            float4( 1,  1, -1, 1),
            float4(-1,  1, -1, 1),
            float4(-1, -1,  1, 1),
            float4( 1, -1,  1, 1),
            float4( 1,  1,  1, 1),
            float4(-1,  1,  1, 1),
        };
        
        float3 corners[8];
        for (int i = 0; i < 8; i++) {
            float4 worldPos = mul(cmd.invViewProj, ndc[i]);
            corners[i] = worldPos.xyz / worldPos.w;
        }
        
        int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7}
        };
        
        for (int e = 0; e < 12; e++) {
            EmitLine(corners[edges[e][0]], corners[edges[e][1]], cmd.color, depthTest);
        }
    }
    
    // Process crosses (3 lines)
    if (tid < counts.crossCount) {
        DrawCrossCmd cmd = crossCommands[tid];
        bool depthTest = cmd.depthTest != 0;
        
        EmitLine(cmd.center - float3(cmd.size, 0, 0), cmd.center + float3(cmd.size, 0, 0), cmd.color, depthTest);
        EmitLine(cmd.center - float3(0, cmd.size, 0), cmd.center + float3(0, cmd.size, 0), cmd.color, depthTest);
        EmitLine(cmd.center - float3(0, 0, cmd.size), cmd.center + float3(0, 0, cmd.size), cmd.color, depthTest);
    }
}
