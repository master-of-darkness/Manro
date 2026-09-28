#include "SwapchainManager.h"
#include "../Vulkan/VulkanContext.h"

#include <Manro/Core/Logger.h>
#include <VkBootstrap.h>
#include <stdexcept>

namespace Manro {
    CSwapchainManager::CSwapchainManager(CVulkanContext &ctx)
        : m_Context(ctx) {
    }

    void CSwapchainManager::Init(u32 width, u32 height, bool vsync) {
        vkb::SwapchainBuilder builder{
            m_Context.GetPhysicalDevice(),
            m_Context.GetDevice(),
            m_Context.GetSurface()
        };

        if (vsync) {
            builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
        } else {
            // Mailbox first: uncapped like immediate but tear-free, and it
            // recycles images promptly (no acquire stalls under compositors).
            builder
                    .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
                    .add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
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
        // Log the negotiated present mode: immediate/mailbox run uncapped,
        // fifo caps at display refresh. First suspect in any fps report.
        {
            const char *pm = "?";
            switch (vkbSwapchain.present_mode) {
                case VK_PRESENT_MODE_IMMEDIATE_KHR: pm = "immediate"; break;
                case VK_PRESENT_MODE_MAILBOX_KHR: pm = "mailbox"; break;
                case VK_PRESENT_MODE_FIFO_KHR: pm = "fifo"; break;
                case VK_PRESENT_MODE_FIFO_RELAXED_KHR: pm = "fifo_relaxed"; break;
                default: break;
            }
            LOG_INFO("[Swapchain] present={} images={} extent={}x{}", pm,
                     vkbSwapchain.image_count, vkbSwapchain.extent.width,
                     vkbSwapchain.extent.height);
        }

        auto imagesRet = vkbSwapchain.get_images();
        auto imageViewsRet = vkbSwapchain.get_image_views();
        if (!imagesRet || !imageViewsRet)
            throw std::runtime_error("Failed to get swapchain images/views");

        m_SwapchainImages = imagesRet.value();
        m_SwapchainImageViews = imageViewsRet.value();
        m_SwapchainImageLayouts.assign(m_SwapchainImages.size(), VK_IMAGE_LAYOUT_UNDEFINED);
        m_bNeedsRecreate = false;
    }

    void CSwapchainManager::Cleanup() {
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

    void CSwapchainManager::CreateRenderFinishedSemaphores() {
        VkDevice device = m_Context.GetDevice();
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        m_RenderFinishedSemaphores.resize(m_SwapchainImages.size());
        for (auto &sem: m_RenderFinishedSemaphores) {
            if (vkCreateSemaphore(device, &si, nullptr, &sem) != VK_SUCCESS)
                throw std::runtime_error("Failed to create render-finished semaphore");
        }
    }

    void CSwapchainManager::DestroyRenderFinishedSemaphores() {
        VkDevice device = m_Context.GetDevice();
        for (auto sem: m_RenderFinishedSemaphores) {
            if (sem != VK_NULL_HANDLE)
                vkDestroySemaphore(device, sem, nullptr);
        }
        m_RenderFinishedSemaphores.clear();
    }

    void CSwapchainManager::CreateFrameSyncObjects(u32 frameCount) {
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

    void CSwapchainManager::DestroyFrameSyncObjects() {
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

    void CSwapchainManager::Shutdown() {
        DestroyFrameSyncObjects();
        DestroyRenderFinishedSemaphores();
        Cleanup();
    }

    void CSwapchainManager::Recreate(u32 width, u32 height, bool vsync) {
        vkDeviceWaitIdle(m_Context.GetDevice());
        DestroyRenderFinishedSemaphores();
        Cleanup();
        Init(width, height, vsync);
        CreateRenderFinishedSemaphores();
    }
} // namespace Manro