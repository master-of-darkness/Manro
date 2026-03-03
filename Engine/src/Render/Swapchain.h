#pragma once
#include <volk.h>
#include <Core/Types.h>
#include <vector>

namespace Engine {
    class VulkanContext;

    class Swapchain {
    public:
        Swapchain(const VulkanContext &context);

        ~Swapchain();

        void Initialize(u32 width, u32 height);

        void Recreate(u32 width, u32 height);

        void Shutdown();

        u32 AcquireNextImage(VkSemaphore presentCompleteSemaphore);

        void Present(u32 imageIndex, VkSemaphore renderCompleteSemaphore);

        VkSwapchainKHR GetHandle() const { return m_Swapchain; }
        size_t GetImageCount() const { return m_Images.size(); }
        VkFormat GetImageFormat() const { return m_ImageFormat; }
        VkExtent2D GetExtent() const { return m_Extent; }
        VkImage GetImage(u32 index) const { return m_Images[index]; }
        VkImageView GetImageView(u32 index) const { return m_ImageViews[index]; }

    private:
        const VulkanContext &m_Context;

        VkSwapchainKHR m_Swapchain{nullptr};
        std::vector<VkImage> m_Images;
        std::vector<VkImageView> m_ImageViews;

        VkFormat m_ImageFormat;
        VkExtent2D m_Extent;
    };
} // namespace Engine