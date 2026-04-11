#include "SwapchainManager.h"
#include "../Vulkan/VulkanContext.h"

#include <VkBootstrap.h>
#include <stdexcept>

namespace Manro {
    SwapchainManager::SwapchainManager(VulkanContext &ctx)
        : m_Context(ctx) {
    }

    void SwapchainManager::Init(u32 width, u32 height, bool vsync) {
        vkb::SwapchainBuilder builder{
            m_Context.GetPhysicalDevice(),
            m_Context.GetDevice(),
            m_Context.GetSurface()
        };

        if (vsync) {
            builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
        } else {
            builder
                    .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
                    .add_fallback_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
                    .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
        }

        auto ret = builder
                .use_default_format_selection()
                .set_desired_extent(width, height)
                .add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build();

        if (!ret)
            throw std::runtime_error("Failed to create Vulkan swapchain");

        vkb::Swapchain vkbSwapchain = ret.value();
        m_Swapchain = vkbSwapchain.swapchain;
        m_SwapchainExtent = vkbSwapchain.extent;
        m_SwapchainFormat = vkbSwapchain.image_format;

        auto imagesRet = vkbSwapchain.get_images();
        auto imageViewsRet = vkbSwapchain.get_image_views();
        if (!imagesRet || !imageViewsRet)
            throw std::runtime_error("Failed to get swapchain images/views");

        m_SwapchainImages = imagesRet.value();
        m_SwapchainImageViews = imageViewsRet.value();
        m_SwapchainImageLayouts.assign(m_SwapchainImages.size(), VK_IMAGE_LAYOUT_UNDEFINED);
        m_NeedsRecreate = false;
    }

    void SwapchainManager::Cleanup() {
        VkDevice device = m_Context.GetDevice();
        for (auto view: m_SwapchainImageViews) {
            if (view != VK_NULL_HANDLE)
                vkDestroyImageView(device, view, nullptr);
        }
        m_SwapchainImageViews.clear();
        m_SwapchainImages.clear();
        m_SwapchainImageLayouts.clear();
        if (m_Swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
    }

    void SwapchainManager::CreateRenderFinishedSemaphores() {
        VkDevice device = m_Context.GetDevice();
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        m_RenderFinishedSemaphores.resize(m_SwapchainImages.size());
        for (auto &sem: m_RenderFinishedSemaphores) {
            if (vkCreateSemaphore(device, &si, nullptr, &sem) != VK_SUCCESS)
                throw std::runtime_error("Failed to create render-finished semaphore");
        }
    }

    void SwapchainManager::DestroyRenderFinishedSemaphores() {
        VkDevice device = m_Context.GetDevice();
        for (auto sem: m_RenderFinishedSemaphores) {
            if (sem != VK_NULL_HANDLE)
                vkDestroySemaphore(device, sem, nullptr);
        }
        m_RenderFinishedSemaphores.clear();
    }

    void SwapchainManager::CreateFrameSyncObjects(u32 frameCount) {
        VkDevice device = m_Context.GetDevice();

        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        m_ImageAvailableSemaphores.resize(frameCount);
        m_InFlightFences.resize(frameCount);

        for (u32 i = 0; i < frameCount; ++i) {
            if (vkCreateSemaphore(device, &si, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create image-available semaphore");
            if (vkCreateFence(device, &fi, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create in-flight fence");
        }
    }

    void SwapchainManager::DestroyFrameSyncObjects() {
        VkDevice device = m_Context.GetDevice();
        for (auto sem: m_ImageAvailableSemaphores) {
            if (sem != VK_NULL_HANDLE)
                vkDestroySemaphore(device, sem, nullptr);
        }
        m_ImageAvailableSemaphores.clear();
        for (auto fence: m_InFlightFences) {
            if (fence != VK_NULL_HANDLE)
                vkDestroyFence(device, fence, nullptr);
        }
        m_InFlightFences.clear();
    }

    void SwapchainManager::Shutdown() {
        DestroyFrameSyncObjects();
        DestroyRenderFinishedSemaphores();
        Cleanup();
    }

    void SwapchainManager::Recreate(u32 width, u32 height, bool vsync) {
        vkDeviceWaitIdle(m_Context.GetDevice());
        DestroyRenderFinishedSemaphores();
        Cleanup();
        Init(width, height, vsync);
        CreateRenderFinishedSemaphores();
    }
} // namespace Manro