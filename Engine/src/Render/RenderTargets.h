#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <Core/Types.h>

namespace Engine {
    class VulkanContext;
    class Swapchain;

    class RenderTargets {
    public:
        void Initialize(const VulkanContext& ctx, const Swapchain& swapchain,
                        u32 width, u32 height, VkSampleCountFlagBits msaa);
        void Recreate(VkDevice device, VmaAllocator allocator,
                      const Swapchain& swapchain, u32 width, u32 height);
        void Shutdown(VkDevice device, VmaAllocator allocator);

        VkImageView GetDepthView() const { return m_DepthImageView; }
        VkImageView GetColorView() const { return m_ColorImageView; }
        VkImage GetColorImage() const { return m_ColorImage; }
        VkFormat GetDepthFormat() const { return m_DepthFormat; }
        VkSampleCountFlagBits GetMsaaSamples() const { return m_MsaaSamples; }

    private:
        void CreateDepthResources(VkDevice device, VmaAllocator allocator,
                                  u32 width, u32 height);
        void DestroyDepthResources(VkDevice device, VmaAllocator allocator);

        void CreateColorResources(VkDevice device, VmaAllocator allocator,
                                  const Swapchain& swapchain,
                                  u32 width, u32 height);
        void DestroyColorResources(VkDevice device, VmaAllocator allocator);

        VkImage m_DepthImage{VK_NULL_HANDLE};
        VkImageView m_DepthImageView{VK_NULL_HANDLE};
        VmaAllocation m_DepthAllocation{nullptr};
        VkFormat m_DepthFormat{VK_FORMAT_D32_SFLOAT};

        VkSampleCountFlagBits m_MsaaSamples{VK_SAMPLE_COUNT_1_BIT};

        VkImage m_ColorImage{VK_NULL_HANDLE};
        VkImageView m_ColorImageView{VK_NULL_HANDLE};
        VmaAllocation m_ColorAllocation{nullptr};
    };
} // namespace Engine
