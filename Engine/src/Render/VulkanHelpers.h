#pragma once

#include <Core/Types.h>
#include <vk_mem_alloc.h>
#include <functional>

namespace Engine {
    class VulkanContext;

    struct ImageCreateParams {
        u32 width{0};
        u32 height{0};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkImageUsageFlags usage{0};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    };

    struct AllocatedImage {
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VmaAllocation allocation{nullptr};
    };

    AllocatedImage CreateImage(const VulkanContext &ctx, const ImageCreateParams &params,
                               VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    void DestroyImage(const VulkanContext &ctx, AllocatedImage &img);

    using OneShotWork = std::function<void(VkCommandBuffer cmd)>;

    void ExecuteOneShot(const VulkanContext &ctx, const OneShotWork &work);
} // namespace Engine
