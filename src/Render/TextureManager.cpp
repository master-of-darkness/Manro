#include <Manro/Render/TextureManager.h>
#include <Manro/Render/Vulkan/VulkanContext.h>
#include <Manro/Render/Vulkan/VulkanHelpers.h>
#include <Manro/Core/Logger.h>
#include <cstring>
#include <stdexcept>

#include "volk.h"

namespace Engine {
    TextureManager::TextureManager(const VulkanContext &ctx) : m_Context(ctx) {
        CreateDefaultSampler();
        CreateWhiteTexture();
    }

    TextureManager::~TextureManager() {
        for (auto &[id, tex]: m_Textures) {
            if (tex.view) vkDestroyImageView(m_Context.GetDevice(), tex.view, nullptr);
            if (tex.image) vmaDestroyImage(m_Context.GetAllocator(), tex.image, tex.allocation);
        }
        m_Textures.clear();

        if (m_Sampler) vkDestroySampler(m_Context.GetDevice(), m_Sampler, nullptr);
    }

    void TextureManager::CreateDefaultSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

        if (vkCreateSampler(m_Context.GetDevice(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
            throw std::runtime_error("[TextureManager] Failed to create texture sampler!");
    }

    void TextureManager::CreateWhiteTexture() {
        const u8 whitePixel[4] = {255, 255, 255, 255};
        m_WhiteTextureId = Upload(whitePixel, 1, 1);
    }

    TextureHandle TextureManager::Upload(const TextureData &data) {
        if (data.pixels.empty() || data.width <= 0 || data.height <= 0) return kInvalidTexture;
        return Upload(data.pixels.data(), data.width, data.height);
    }

    TextureHandle TextureManager::Upload(const u8 *pixels, int width, int height) {
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

        // Staging buffer
        VkBufferCreateInfo stagingBufInfo{};
        stagingBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufInfo.size = imageSize;
        stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer stagingBuf{};
        VmaAllocation stagingAlloc{};
        VmaAllocationInfo stagingAllocRes{};

        if (vmaCreateBuffer(m_Context.GetAllocator(), &stagingBufInfo, &stagingAllocInfo,
                            &stagingBuf, &stagingAlloc, &stagingAllocRes) != VK_SUCCESS) {
            LOG_ERROR("[TextureManager] Failed to create texture staging buffer!");
            return kInvalidTexture;
        }
        std::memcpy(stagingAllocRes.pMappedData, pixels, imageSize);

        // GPU image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<u32>(width), static_cast<u32>(height), 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo gpuAllocInfo{};
        gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        LoadedTexture tex{};
        if (vmaCreateImage(m_Context.GetAllocator(), &imageInfo, &gpuAllocInfo,
                           &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
            LOG_ERROR("[TextureManager] Failed to create VkImage!");
            vmaDestroyBuffer(m_Context.GetAllocator(), stagingBuf, stagingAlloc);
            return kInvalidTexture;
        }

        // Transfer via one-shot command
        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            // Transition to transfer dst
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Copy
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {static_cast<u32>(width), static_cast<u32>(height), 1};
            vkCmdCopyBufferToImage(cmd, stagingBuf, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            // Transition to shader read
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });

        vmaDestroyBuffer(m_Context.GetAllocator(), stagingBuf, stagingAlloc);

        // Image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = tex.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        if (vkCreateImageView(m_Context.GetDevice(), &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
            LOG_ERROR("[TextureManager] Failed to create texture image view!");
            return kInvalidTexture;
        }

        TextureHandle id = m_NextId++;
        m_Textures.emplace(id, std::move(tex));
        return id;
    }

    VkImageView TextureManager::GetView(TextureHandle handle) const {
        auto it = m_Textures.find(handle);
        if (it != m_Textures.end()) return it->second.view;
        
        auto whiteIt = m_Textures.find(m_WhiteTextureId);
        return (whiteIt != m_Textures.end()) ? whiteIt->second.view : VK_NULL_HANDLE;
    }

    void TextureManager::ResetBinding() {
    }
} // namespace Engine
