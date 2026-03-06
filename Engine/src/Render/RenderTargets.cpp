#include "RenderTargets.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include <stdexcept>

namespace Engine {
    void RenderTargets::Initialize(const VulkanContext& ctx, const Swapchain& swapchain,
                                   u32 width, u32 height, VkSampleCountFlagBits msaa) {
        m_MsaaSamples = msaa;
        CreateColorResources(ctx.GetDevice(), ctx.GetAllocator(), swapchain, width, height);
        CreateDepthResources(ctx.GetDevice(), ctx.GetAllocator(), width, height);
    }

    void RenderTargets::Recreate(VkDevice device, VmaAllocator allocator,
                                  const Swapchain& swapchain, u32 width, u32 height) {
        DestroyColorResources(device, allocator);
        CreateColorResources(device, allocator, swapchain, width, height);
        DestroyDepthResources(device, allocator);
        CreateDepthResources(device, allocator, width, height);
    }

    void RenderTargets::Shutdown(VkDevice device, VmaAllocator allocator) {
        DestroyColorResources(device, allocator);
        DestroyDepthResources(device, allocator);
    }

    void RenderTargets::CreateDepthResources(VkDevice device, VmaAllocator allocator,
                                             u32 width, u32 height) {
        VkImageCreateInfo depthInfo{};
        depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthInfo.imageType = VK_IMAGE_TYPE_2D;
        depthInfo.extent = {width, height, 1};
        depthInfo.mipLevels = 1;
        depthInfo.arrayLayers = 1;
        depthInfo.format = m_DepthFormat;
        depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthInfo.samples = m_MsaaSamples;
        depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo depthAllocInfo{};
        depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        depthAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateImage(allocator, &depthInfo, &depthAllocInfo,
                           &m_DepthImage, &m_DepthAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("[RenderTargets] Failed to create depth image!");
        }

        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = m_DepthImage;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = m_DepthFormat;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthViewInfo.subresourceRange.baseMipLevel = 0;
        depthViewInfo.subresourceRange.levelCount = 1;
        depthViewInfo.subresourceRange.baseArrayLayer = 0;
        depthViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &depthViewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS) {
            throw std::runtime_error("[RenderTargets] Failed to create depth image view!");
        }
    }

    void RenderTargets::DestroyDepthResources(VkDevice device, VmaAllocator allocator) {
        if (m_DepthImageView) {
            vkDestroyImageView(device, m_DepthImageView, nullptr);
            m_DepthImageView = VK_NULL_HANDLE;
        }
        if (m_DepthImage) {
            vmaDestroyImage(allocator, m_DepthImage, m_DepthAllocation);
            m_DepthImage = VK_NULL_HANDLE;
            m_DepthAllocation = nullptr;
        }
    }

    void RenderTargets::CreateColorResources(VkDevice device, VmaAllocator allocator,
                                             const Swapchain& swapchain,
                                             u32 width, u32 height) {
        if (m_MsaaSamples == VK_SAMPLE_COUNT_1_BIT) return;

        VkImageCreateInfo colorInfo{};
        colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorInfo.imageType = VK_IMAGE_TYPE_2D;
        colorInfo.extent.width = width;
        colorInfo.extent.height = height;
        colorInfo.extent.depth = 1;
        colorInfo.mipLevels = 1;
        colorInfo.arrayLayers = 1;
        colorInfo.format = swapchain.GetImageFormat();
        colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        colorInfo.samples = m_MsaaSamples;
        colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo colorAllocInfo{};
        colorAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        colorAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateImage(allocator, &colorInfo, &colorAllocInfo,
                           &m_ColorImage, &m_ColorAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("[RenderTargets] Failed to create multisampled color image!");
        }

        VkImageViewCreateInfo colorViewInfo{};
        colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorViewInfo.image = m_ColorImage;
        colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorViewInfo.format = swapchain.GetImageFormat();
        colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorViewInfo.subresourceRange.baseMipLevel = 0;
        colorViewInfo.subresourceRange.levelCount = 1;
        colorViewInfo.subresourceRange.baseArrayLayer = 0;
        colorViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &colorViewInfo, nullptr, &m_ColorImageView) != VK_SUCCESS) {
            throw std::runtime_error("[RenderTargets] Failed to create multisampled color image view!");
        }
    }

    void RenderTargets::DestroyColorResources(VkDevice device, VmaAllocator allocator) {
        if (m_ColorImageView) {
            vkDestroyImageView(device, m_ColorImageView, nullptr);
            m_ColorImageView = VK_NULL_HANDLE;
        }
        if (m_ColorImage) {
            vmaDestroyImage(allocator, m_ColorImage, m_ColorAllocation);
            m_ColorImage = VK_NULL_HANDLE;
            m_ColorAllocation = nullptr;
        }
    }
} // namespace Engine
