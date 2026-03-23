#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace Manro {
    enum class ToneMapMethod : int {
        Filmic = 0,
        Uncharted2,
        Clip,
        ACES,
        AgX,
        KhronosPBR,
        Count
    };

    struct SlangFloat3x3 {
        glm::vec4 col0{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 col1{0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 col2{0.0f, 0.0f, 1.0f, 0.0f};

        SlangFloat3x3() = default;

        SlangFloat3x3(const glm::mat3 &m) {
            col0 = glm::vec4(m[0], 0.0f);
            col1 = glm::vec4(m[1], 0.0f);
            col2 = glm::vec4(m[2], 0.0f);
        }
    };

    struct TonemapperData {
        int isActive = 1;
        int method = static_cast<int>(ToneMapMethod::Filmic);

        float exposure = 1.0f;
        float temperature = 6506.11144f;
        float tint = 3.25895312e-3f;
        float _pad0[3];

        alignas(16) SlangFloat3x3 inputMatrix;

        // Post effects
        float contrast = 1.0f;
        float brightness = 1.0f;
        float saturation = 1.0f;
        float vignette = 0.0f;

        // Advanced color grading TODO: fix me pls. I don't update onChange
        float vibrance = 0.0f;
        float shadowBias = 0.0f;
        float midtoneBias = 0.0f;
        float highlightBias = 0.0f;

        float coolColor[3] = {1.0f, 1.0f, 1.0f};
        float _pad1 = 0.0f; // pad coolColor to 16 bytes
        float warmColor[3] = {1.0f, 1.0f, 1.0f};
        float splitBalance = 0.0f;

        // Auto-Exposure TODO: make it work
        int autoExposure = 0;
        float autoExposureSpeed = 5.0f;
        float evMinValue = -5.0f;
        float evMaxValue = 10.0f;

        uint32_t enableCenterMetering = 0;
        float centerMeteringSize = 0.5f;
        uint32_t averageMode = 1;
        int dither = 1;
    };

    struct CompositePushConstants {
        TonemapperData tm;
        glm::vec2 imageSize;
        float _pad[2];
    };
} // namespace Manro
