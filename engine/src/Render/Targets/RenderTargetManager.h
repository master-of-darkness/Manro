#pragma once

#include <Manro/Core/Types.h>
#include "../Vulkan/VulkanHelpers.h"
#include <volk.h>

namespace Manro {
    class CVulkanContext;

    class CRenderTargetManager {
    public:
        explicit CRenderTargetManager(CVulkanContext &ctx);

        ~CRenderTargetManager() = default;

        CRenderTargetManager(const CRenderTargetManager &) = delete;

        CRenderTargetManager &operator=(const CRenderTargetManager &) = delete;

        void Create(u32 width, u32 height, VkSampleCountFlagBits samples);

        void Destroy();

        VkImageView GetOffscreenView() const { return m_OffscreenColor.view; }
        VkImageView GetDepthView() const { return m_DepthImage.view; }
        VkImageView GetMsaaView() const { return m_MsaaColorImage.view; }
        VkSampler GetOffscreenSampler() const { return m_OffscreenSampler; }
        VkFormat GetOffscreenFormat() const { return m_OffscreenFormat; }
        VkFormat GetDepthFormat() const { return m_DepthFormat; }
        VkImage GetOffscreenImage() const { return m_OffscreenColor.image; }
        VkImage GetMsaaImage() const { return m_MsaaColorImage.image; }

    private:
        void CreateOffscreen(u32 w, u32 h);

        void CreateDepth(u32 w, u32 h, VkSampleCountFlagBits samples);

        void CreateMsaaColor(u32 w, u32 h, VkSampleCountFlagBits samples);

        CVulkanContext &m_Context;

        AllocatedImage_t m_OffscreenColor{};
        AllocatedImage_t m_MsaaColorImage{};
        AllocatedImage_t m_DepthImage{};

        VkSampler m_OffscreenSampler{VK_NULL_HANDLE};

        VkFormat m_OffscreenFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
        VkFormat m_DepthFormat{VK_FORMAT_D32_SFLOAT};

        VkSampleCountFlagBits m_LastSamples{VK_SAMPLE_COUNT_1_BIT};
    };
} // namespace Manro