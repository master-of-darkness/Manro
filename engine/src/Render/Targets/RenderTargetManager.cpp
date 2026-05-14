#include "RenderTargetManager.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/VulkanHelpers.h"

#include <stdexcept>

namespace Manro {
    CRenderTargetManager::CRenderTargetManager(CVulkanContext &ctx)
        : m_Context(ctx) {
    }

    void CRenderTargetManager::Create(u32 width, u32 height, VkSampleCountFlagBits samples) {
        m_LastSamples = samples;
        CreateOffscreen(width, height);
        CreateDepth(width, height, samples);
        CreateMsaaColor(width, height, samples);
    }

    void CRenderTargetManager::Destroy() {
        VkDevice device = m_Context.GetDevice();

        if (m_OffscreenSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_OffscreenSampler, nullptr);
            m_OffscreenSampler = VK_NULL_HANDLE;
        }

        DestroyImage(m_Context, m_OffscreenColor);
        DestroyImage(m_Context, m_MsaaColorImage);
        DestroyImage(m_Context, m_DepthImage);
        m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void CRenderTargetManager::CreateOffscreen(u32 w, u32 h) {
        ImageCreateParams_t p{};
        p.width = w;
        p.height = h;
        p.format = m_OffscreenFormat;
        p.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        p.samples = VK_SAMPLE_COUNT_1_BIT;
        m_OffscreenColor = CreateImage(m_Context, p);

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = VK_LOD_CLAMP_NONE;
        if (vkCreateSampler(m_Context.GetDevice(), &si, nullptr, &m_OffscreenSampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create offscreen sampler");

        // Transition to SHADER_READ_ONLY_OPTIMAL so it can be bound immediately
        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_OffscreenColor.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        });
    }

    void CRenderTargetManager::CreateDepth(u32 w, u32 h, VkSampleCountFlagBits samples) {
        ImageCreateParams_t p{};
        p.width = w;
        p.height = h;
        p.format = m_DepthFormat;
        p.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        p.samples = samples;
        m_DepthImage = CreateImage(m_Context, p, VK_IMAGE_ASPECT_DEPTH_BIT);
        m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void CRenderTargetManager::CreateMsaaColor(u32 w, u32 h, VkSampleCountFlagBits samples) {
        if (samples == VK_SAMPLE_COUNT_1_BIT) return;

        ImageCreateParams_t p{};
        p.width = w;
        p.height = h;
        p.format = m_OffscreenFormat;
        p.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        p.samples = samples;
        m_MsaaColorImage = CreateImage(m_Context, p);
    }
} // namespace Manro
