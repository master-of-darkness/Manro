#pragma once

#include <Manro/Render/Tonemap/Tonemapper.h>

namespace Manro {
    enum class AntiAliasingMode : int {
        None = 0,
        MSAA,
        FXAA,
        TAA,
        Count
    };

    enum class MSAASampleCount : int {
        MSAA_1X = 1,
        MSAA_2X = 2,
        MSAA_4X = 4,
        MSAA_8X = 8,
        MSAA_16X = 16,
        MSAA_32X = 32,
        MSAA_64X = 64
    };

    struct ShadowSettings {
        bool enabled = true;
        int resolution = 2048;
        float bias = 0.005f;
        float slopeBias = 0.05f;
        float softShadows = 1.0f;
    };

    struct LightingSettings {
        float iblIntensity = 1.0f;
        float gamma = 2.2f;
        bool enableAmbientOcclusion = false;
        float aoIntensity = 1.0f;
        float aoRadius = 0.5f;
    };

    struct PostProcessSettings {
        bool enableBloom = false;
        float bloomIntensity = 1.0f;
        float bloomThreshold = 1.0f;
        TonemapperData tonemapping{};
    };

    struct RayTracingSettings {
        bool enableReflections = false;
        bool enableTransparency = false;
        int maxBounces = 2;
    };

    struct RenderSettings {
        float resolutionScale = 1.0f;
        AntiAliasingMode aaMode = AntiAliasingMode::MSAA;
        MSAASampleCount msaaSamples = MSAASampleCount::MSAA_4X;

        bool enableVSync = true;
        bool enableFrustumCulling = true;

        float nearZ = 0.1f;
        float farZ = 10000.0f;
        float maxDrawDistance = 10000.0f;

        ShadowSettings shadows;
        LightingSettings lighting;
        PostProcessSettings postProcess;
        RayTracingSettings rayTracing;
    };
} // namespace Manro