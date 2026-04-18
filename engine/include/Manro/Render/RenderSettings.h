#pragma once

#include <Manro/Render/Tonemap/Tonemapper.h>

namespace Manro {
    enum class AntiAliasingMode : int {
        None = 0,
        MSAA,
        FXAA, // TODO
        TAA, // TODO
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

    struct ShadowSettings_t {
        bool enabled = true;
        int resolution = 2048;
        float bias = 0.005f;
        float slopeBias = 0.05f;
        float softShadows = 1.0f;
    };

    struct LightingSettings_t {
        float iblIntensity = 1.0f;
        float gamma = 2.2f;
        bool enableAmbientOcclusion = false;
        float aoIntensity = 1.0f;
        float aoRadius = 0.5f;
    };

    struct PostProcessSettings_t {
        bool enableBloom = false;
        float bloomIntensity = 1.0f;
        float bloomThreshold = 1.0f;
        TonemapperData_t tonemapping{};
    };

    struct TextureSettings_t {
        float anisotropy = 16.0f;
    };

    struct RayTracingSettings_t { // TODO
        bool enableReflections = false;
        bool enableTransparency = false;
        int maxBounces = 2;
    };

    struct RenderSettings_t {
        float resolutionScale = 1.0f;
        AntiAliasingMode aaMode = AntiAliasingMode::MSAA;
        MSAASampleCount msaaSamples = MSAASampleCount::MSAA_4X;

        bool enableVSync = false;
        bool enableFrustumCulling = true;

        float nearZ = 0.1f;
        float farZ = 10000.0f;
        float maxDrawDistance = 10000.0f;

        ShadowSettings_t shadows;
        LightingSettings_t lighting;
        PostProcessSettings_t postProcess;
        RayTracingSettings_t rayTracing;
        TextureSettings_t textures;
    };
} // namespace Manro