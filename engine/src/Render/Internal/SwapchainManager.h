#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>
#include <vector>

namespace Manro {
    class CVulkanContext;

    class CSwapchainManager {
    public:
        explicit CSwapchainManager(CVulkanContext &ctx);

        ~CSwapchainManager() = default;

        CSwapchainManager(const CSwapchainManager &) = delete;

        CSwapchainManager &operator=(const CSwapchainManager &) = delete;

        void Init(u32 width, u32 height, bool vsync);

        void Cleanup();

        void CreateRenderFinishedSemaphores();

        void DestroyRenderFinishedSemaphores();

        void CreateFrameSyncObjects(u32 frameCount);

        void DestroyFrameSyncObjects();

        void Shutdown();

        void Recreate(u32 width, u32 height, bool vsync);

        VkSwapchainKHR GetHandle() const { return m_Swapchain; }
        VkFormat GetFormat() const { return m_SwapchainFormat; }
        VkExtent2D GetExtent() const { return m_SwapchainExtent; }
        u32 GetImageCount() const { return static_cast<u32>(m_SwapchainImages.size()); }
        VkImage GetImage(u32 i) const { return m_SwapchainImages[i]; }
        VkImageView GetImageView(u32 i) const { return m_SwapchainImageViews[i]; }
        VkImageLayout GetImageLayout(u32 i) const { return m_SwapchainImageLayouts[i]; }
        void SetImageLayout(u32 i, VkImageLayout l) { m_SwapchainImageLayouts[i] = l; }
        VkSemaphore GetImageAvailableSemaphore(u32 i) const { return m_ImageAvailableSemaphores[i]; }
        VkSemaphore GetRenderFinishedSemaphore(u32 i) const { return m_RenderFinishedSemaphores[i]; }
        VkFence GetInFlightFence(u32 i) const { return m_InFlightFences[i]; }
        bool NeedsRecreate() const { return m_bNeedsRecreate; }
        void SetNeedsRecreate(bool v) { m_bNeedsRecreate = v; }

        const std::vector<VkImage> &GetImages() const { return m_SwapchainImages; }

    private:
        CVulkanContext &m_Context;

        VkSwapchainKHR m_Swapchain{VK_NULL_HANDLE};
        VkFormat m_SwapchainFormat{VK_FORMAT_UNDEFINED};
        VkExtent2D m_SwapchainExtent{};
        std::vector<VkImage> m_SwapchainImages;
        std::vector<VkImageView> m_SwapchainImageViews;
        std::vector<VkImageLayout> m_SwapchainImageLayouts;
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;
        bool m_bNeedsRecreate{false};
    };
} // namespace Manro