#include <Manro/Render/TextureManager.h>
#include "Backend/Vulkan/VulkanContext.h"
#include "Backend/Vulkan/VulkanHelpers.h"
#include "Backend/Vulkan/DescriptorAllocator.h"
#include <Manro/Core/Logger.h>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

#include "volk.h"

namespace Manro {

    struct TextureManager::Impl {
        const VulkanContext &context;
        BindlessAllocator &bindlessAlloc;

        struct LoadedTexture {
            VkImage image{VK_NULL_HANDLE};
            VkImageView view{VK_NULL_HANDLE};
            VmaAllocation allocation{nullptr};
        };

        std::unordered_map<TextureHandle, LoadedTexture> textures;
        TextureHandle nextId{0};

        VkSampler sampler{VK_NULL_HANDLE};
        TextureHandle whiteTextureId{kInvalidTexture};

        Impl(const VulkanContext &ctx, BindlessAllocator &alloc)
                : context(ctx), bindlessAlloc(alloc) {}
    };

    TextureManager::TextureManager(const VulkanContext &ctx, BindlessAllocator &bindlessAlloc)
            : m_Impl(new Impl(ctx, bindlessAlloc)) {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.maxLod = VK_LOD_CLAMP_NONE;

        if (vkCreateSampler(m_Impl->context.GetDevice(), &si, nullptr, &m_Impl->sampler) != VK_SUCCESS)
            throw std::runtime_error("[TextureManager] Failed to create default sampler");
    }

    void TextureManager::InitDefaults() {
        if (m_Impl->whiteTextureId != kInvalidTexture) return;
        const u8 pixels[4] = {255, 0, 255, 255};
        m_Impl->whiteTextureId = Upload(pixels, 1, 1);
    }

    TextureManager::~TextureManager() {
        for (auto &[id, tex]: m_Impl->textures) {
            if (tex.view) vkDestroyImageView(m_Impl->context.GetDevice(), tex.view, nullptr);
            if (tex.image) vmaDestroyImage(m_Impl->context.GetAllocator(), tex.image, tex.allocation);
        }
        m_Impl->textures.clear();

        if (m_Impl->sampler) vkDestroySampler(m_Impl->context.GetDevice(), m_Impl->sampler, nullptr);

        delete m_Impl;
    }

    VkDescriptorSet TextureManager::GetBindlessSet() const {
        return m_Impl->bindlessAlloc.GetSet();
    }

    VkDescriptorSetLayout TextureManager::GetBindlessLayout() const {
        return m_Impl->bindlessAlloc.GetLayout();
    }

    VkSampler TextureManager::GetSampler() const {
        return m_Impl->sampler;
    }

    TextureHandle TextureManager::GetWhiteTextureId() const {
        return m_Impl->whiteTextureId;
    }

    TextureHandle TextureManager::Upload(const TextureData &data) {
        if (data.pixels.empty() || data.width <= 0 || data.height <= 0)
            return kInvalidTexture;
        return Upload(data.pixels.data(), data.width, data.height);
    }

    TextureHandle TextureManager::Upload(const u8 *pixels, int width, int height) {
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

        VkBuffer stagingBuf{};
        VmaAllocation stagingAlloc{};
        VmaAllocationInfo stagingAllocInfo{};

        VkBufferCreateInfo stagingCI{};
        stagingCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingCI.size = imageSize;
        stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocCI{};
        stagingAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        stagingAllocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if (vmaCreateBuffer(m_Impl->context.GetAllocator(),
                            &stagingCI, &stagingAllocCI,
                            &stagingBuf, &stagingAlloc, &stagingAllocInfo) != VK_SUCCESS) {
            LOG_ERROR("[TextureManager] Failed to create staging buffer");
            return kInvalidTexture;
        }

        std::memcpy(stagingAllocInfo.pMappedData, pixels, imageSize);
        vmaFlushAllocation(m_Impl->context.GetAllocator(), stagingAlloc, 0, imageSize);

        VkImageCreateInfo imageCI{};
        imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCI.imageType = VK_IMAGE_TYPE_2D;
        imageCI.extent = {static_cast<u32>(width), static_cast<u32>(height), 1};
        imageCI.mipLevels = 1;
        imageCI.arrayLayers = 1;
        imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo gpuAllocCI{};
        gpuAllocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        Impl::LoadedTexture tex{};
        if (vmaCreateImage(m_Impl->context.GetAllocator(), &imageCI, &gpuAllocCI,
                           &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
            LOG_ERROR("[TextureManager] Failed to create VkImage");
            vmaDestroyBuffer(m_Impl->context.GetAllocator(), stagingBuf, stagingAlloc);
            return kInvalidTexture;
        }

        ExecuteOneShot(m_Impl->context, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {static_cast<u32>(width), static_cast<u32>(height), 1};
            vkCmdCopyBufferToImage(cmd, stagingBuf, tex.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });

        vmaDestroyBuffer(m_Impl->context.GetAllocator(), stagingBuf, stagingAlloc);

        VkImageViewCreateInfo viewCI{};
        viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image = tex.image;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        if (vkCreateImageView(m_Impl->context.GetDevice(), &viewCI, nullptr, &tex.view) != VK_SUCCESS) {
            LOG_ERROR("[TextureManager] Failed to create image view");
            vmaDestroyImage(m_Impl->context.GetAllocator(), tex.image, tex.allocation);
            return kInvalidTexture;
        }

        TextureHandle id = m_Impl->nextId++;
        m_Impl->textures.emplace(id, tex);
        m_Impl->bindlessAlloc.UpdateSlot(id, m_Impl->textures[id].view);

        return id;
    }

    VkImageView TextureManager::GetView(TextureHandle handle) const {
        auto it = m_Impl->textures.find(handle);
        if (it != m_Impl->textures.end()) return it->second.view;

        auto white = m_Impl->textures.find(m_Impl->whiteTextureId);
        return (white != m_Impl->textures.end()) ? white->second.view : VK_NULL_HANDLE;
    }

} // namespace Manro