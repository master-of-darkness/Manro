#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/Tonemap/Tonemapper.h>
#include <vulkan/vulkan.h>

namespace Manro {
    struct RenderSettings {
        float resolutionScale = 1.0f;

        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

        TonemapperData postProcessing{};
    };
} // namespace Manro