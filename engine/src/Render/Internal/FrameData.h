#pragma once

#include <Manro/Core/Types.h>
#include "../Vulkan/Buffer.h"

#include <volk.h>

namespace Manro {
    struct FrameData_t {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        Scope<CBuffer> uboBuffer;
        Scope<CBuffer> lightBuffer;
        Scope<CBuffer> instanceBuffer;
        Scope<CBuffer> cullDataBuffer;
        Scope<CBuffer> indirectBuffer;
        Scope<CBuffer> countBuffer;
        Scope<CBuffer> tileHeaderBuffer;
        Scope<CBuffer> tileLightIndexBuffer;
        Scope<CBuffer> shadowIndirectBuffer;
        Scope<CBuffer> shadowCountBuffer;

        VkDescriptorSet pbrSet = VK_NULL_HANDLE;
        VkDescriptorSet cullSet = VK_NULL_HANDLE;
        VkDescriptorSet meshCullSet = VK_NULL_HANDLE;
        VkDescriptorSet compositeSet = VK_NULL_HANDLE;
        VkDescriptorSet shadowMeshCullSet = VK_NULL_HANDLE;
        VkDescriptorSet skyboxSet = VK_NULL_HANDLE;
        VkDescriptorSet autoExposureSet = VK_NULL_HANDLE;

        u32 staticUploadedGeneration = 0;
        u32 lightUploadedGeneration = 0;
    };
} // namespace Manro
