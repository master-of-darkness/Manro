#pragma once

#include <Manro/Core/Types.h>
#include <vk_mem_alloc.h>
#include <functional>
#include <string>

namespace Manro {
    class CVulkanContext;

    struct ImageCreateParams_t {
        u32 width{0};
        u32 height{0};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkImageUsageFlags usage{0};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    };

    struct AllocatedImage_t {
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VmaAllocation allocation{nullptr};
    };

    AllocatedImage_t CreateImage(const CVulkanContext &ctx, const ImageCreateParams_t &params,
                                 VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    std::vector<u8> ReadBinaryFile(const std::string &filepath);

    void DestroyImage(const CVulkanContext &ctx, AllocatedImage_t &img);

    using OneShotWork = std::function<void(VkCommandBuffer cmd)>;

    void ExecuteOneShot(const CVulkanContext &ctx, const OneShotWork &work);
} // namespace Manro
