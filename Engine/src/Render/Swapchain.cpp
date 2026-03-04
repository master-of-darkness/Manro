#include "Swapchain.h"
#include "VulkanContext.h"
#include <VkBootstrap.h>
#include <Core/Logger.h>
#include <stdexcept>

namespace Engine {
    Swapchain::Swapchain(const VulkanContext &context)
        : m_Context(context) {
    }

    Swapchain::~Swapchain() {
        Shutdown();
    }

    void Swapchain::Initialize(u32 width, u32 height) {
        vkb::SwapchainBuilder swapchain_builder{
            m_Context.GetPhysicalDevice(), m_Context.GetDevice(), m_Context.GetSurface()
        };
        auto vkb_swapchain_ret = swapchain_builder
                .use_default_format_selection()
                .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                .set_desired_extent(width, height)
                .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build();

        if (!vkb_swapchain_ret) {
            LOG_ERROR("Failed to create swapchain!\n{}", vkb_swapchain_ret.error().message());
            throw std::runtime_error("Swapchain creation failed");
        }

        vkb::Swapchain vkb_swapchain = vkb_swapchain_ret.value();
        m_Swapchain = vkb_swapchain.swapchain;
        m_ImageFormat = vkb_swapchain.image_format;
        m_Extent = vkb_swapchain.extent;
        m_Images = vkb_swapchain.get_images().value();
        m_ImageViews = vkb_swapchain.get_image_views().value();
    }

    void Swapchain::Recreate(u32 width, u32 height) {
        vkDeviceWaitIdle(m_Context.GetDevice());
        Shutdown();
        Initialize(width, height);
        m_NeedsRecreate = false;
    }

    void Swapchain::Shutdown() {
        if (m_Swapchain == VK_NULL_HANDLE) return;

        for (auto imageView: m_ImageViews)
            vkDestroyImageView(m_Context.GetDevice(), imageView, nullptr);
        m_ImageViews.clear();
        m_Images.clear();

        vkDestroySwapchainKHR(m_Context.GetDevice(), m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }

    u32 Swapchain::AcquireNextImage(VkSemaphore presentCompleteSemaphore) {
        u32 imageIndex;
        VkResult res = vkAcquireNextImageKHR(m_Context.GetDevice(), m_Swapchain, UINT64_MAX,
                                             presentCompleteSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (res == VK_ERROR_OUT_OF_DATE_KHR) {
            m_NeedsRecreate = true;
            return UINT32_MAX;
        }
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("Failed to acquire swapchain image!");

        return imageIndex;
    }

    bool Swapchain::Present(u32 imageIndex, VkSemaphore renderCompleteSemaphore) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderCompleteSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_Swapchain;
        presentInfo.pImageIndices = &imageIndex;

        VkResult res = vkQueuePresentKHR(m_Context.GetGraphicsQueue(), &presentInfo);
        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
            m_NeedsRecreate = true;
            return true;
        }
        if (res != VK_SUCCESS)
            throw std::runtime_error("Failed to present swapchain image!");

        return false;
    }
} // namespace Engine
