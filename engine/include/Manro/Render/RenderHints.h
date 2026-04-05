#pragma once

#include <Manro/Core/Types.h>

namespace Manro {

    enum class RenderPass : u8 {
        ZPrepass,
        Shadow,
        MainGeometry,
        Skybox,
        PostProcess,
        UI
    };

    enum class BatchingStrategy : u8 {
        Default,           // Engine decides
        PreferIndirect,    // Maximize GPU-driven indirect draws
        PreferDirect,      // More CPU draw calls, less GPU overhead
        Hybrid             // Mix based on instance count thresholds
    };

    enum class CullingMode : u8 {
        None,              // No culling (debug)
        CPU,               // CPU frustum culling only
        GPU,               // GPU-driven culling (compute shader)
        Hybrid             // CPU coarse + GPU fine culling
    };

    struct PassHints {
        bool enabled = true;
        u32 priorityOrder = 0;        // Lower = earlier
        CullingMode culling = CullingMode::GPU;
        bool allowAsyncCompute = true;
        u32 maxDrawCalls = 0;         // 0 = unlimited
    };

    struct RenderHints {
        u64 estimatedSceneMemory = 0;     // Hint for staging buffer sizing
        u32 expectedMeshCount = 1000;
        u32 expectedTextureCount = 500;
        u32 expectedLightCount = 100;

        BatchingStrategy batching = BatchingStrategy::Default;
        bool preferLowLatency = false;     // Minimize frame latency vs throughput
        bool allowMultithreading = true;   // Use job system for CPU work

        float lodBias = 0.0f;              // Negative = higher quality
        bool enableOcclusionCulling = true;
        bool enableDistanceCulling = true;
        float maxDrawDistance = 10000.0f;

        bool enableGPUValidation = false;   // Vulkan validation layers
        bool enablePipelineStatistics = false;
    };

} // namespace Manro
