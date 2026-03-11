#pragma once
#include <volk.h>
#include <Core/Types.h>
#include <vector>

namespace Engine {
    class VulkanContext;

    class Swapchain {
    public:
        Swapchain(const VulkanContext &context, u32 width, u32 height);

        ~Swapchain();

        Swapchain(const Swapchain &) = delete;

        Swapchain &operator=(const Swapchain &) = delete;

        void Recreate(u32 width, u32 height);

        void Shutdown();

        u32 AcquireNextImage(VkSemaphore presentCompleteSemaphore);

        bool Present(u32 imageIndex, VkSemaphore renderCompleteSemaphore);

        bool NeedsRecreate() const { return m_NeedsRecreate; }

        VkSwapchainKHR GetHandle() const { return m_Swapchain; }
        size_t GetImageCount() const { return m_Images.size(); }
        VkFormat GetImageFormat() const { return m_ImageFormat; }
        VkExtent2D GetExtent() const { return m_Extent; }
        VkImage GetImage(u32 index) const { return m_Images[index]; }
        VkImageView GetImageView(u32 index) const { return m_ImageViews[index]; }

    private:
        void Build(u32 width, u32 height);

        const VulkanContext &m_Context;

        VkSwapchainKHR m_Swapchain{VK_NULL_HANDLE};
        std::vector<VkImage> m_Images;
        std::vector<VkImageView> m_ImageViews;

        VkFormat m_ImageFormat{VK_FORMAT_UNDEFINED};
        VkExtent2D m_Extent{};

        bool m_NeedsRecreate{false};
    };
} // namespace Engine