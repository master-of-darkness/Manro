#include "TextureManager.h"
#include "../../Vulkan/VulkanContext.h"
#include "../../Vulkan/DescriptorAllocator.h"

#include <Manro/Core/Logger.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <volk.h>

namespace Manro {
    struct CTextureManager::Impl_t {
        const CVulkanContext &context;
        CBindlessAllocator &bindlessAlloc;

        struct LoadedTexture_t {
            VkImage image{VK_NULL_HANDLE};
            VkImageView view{VK_NULL_HANDLE};
            VmaAllocation allocation{nullptr};
        };

        struct PendingStagingBuffer_t {
            VkBuffer buffer{VK_NULL_HANDLE};
            VmaAllocation allocation{nullptr};
        };

        std::unordered_map<TextureHandle, LoadedTexture_t> textures;
        TextureHandle nextId{0};

        VkSampler sampler{VK_NULL_HANDLE};
        float currentAnisotropy{1.0f};
        TextureHandle whiteTextureId{kInvalidTexture};

        VkCommandPool transferCommandPool{VK_NULL_HANDLE};
        VkCommandBuffer transferCommandBuffer{VK_NULL_HANDLE};
        VkFence transferFence{VK_NULL_HANDLE};
        bool transferRecording{false};
        std::vector<PendingStagingBuffer_t> pendingStagingBuffers;

        Impl_t(const CVulkanContext &ctx, CBindlessAllocator &alloc)
            : context(ctx), bindlessAlloc(alloc) {
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = context.GetGraphicsQueueFamilyIndex();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            if (vkCreateCommandPool(context.GetDevice(), &poolInfo, nullptr, &transferCommandPool) != VK_SUCCESS) {
                throw std::runtime_error("[CTextureManager] Failed to create transfer command pool");
            }

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = transferCommandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(context.GetDevice(), &allocInfo, &transferCommandBuffer) != VK_SUCCESS) {
                vkDestroyCommandPool(context.GetDevice(), transferCommandPool, nullptr);
                transferCommandPool = VK_NULL_HANDLE;
                throw std::runtime_error("[CTextureManager] Failed to allocate transfer command buffer");
            }

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (vkCreateFence(context.GetDevice(), &fenceInfo, nullptr, &transferFence) != VK_SUCCESS) {
                vkDestroyCommandPool(context.GetDevice(), transferCommandPool, nullptr);
                transferCommandPool = VK_NULL_HANDLE;
                transferCommandBuffer = VK_NULL_HANDLE;
                throw std::runtime_error("[CTextureManager] Failed to create transfer fence");
            }
        }

        bool EnsureTransferRecording() {
            if (transferRecording) return true;

            if (vkResetCommandPool(context.GetDevice(), transferCommandPool, 0) != VK_SUCCESS) {
                LOG_ERROR("[CTextureManager] Failed to reset transfer command pool");
                return false;
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(transferCommandBuffer, &beginInfo) != VK_SUCCESS) {
                LOG_ERROR("[CTextureManager] Failed to begin transfer command buffer");
                return false;
            }

            transferRecording = true;
            return true;
        }

        void DestroyPendingStagingBuffers() {
            for (auto &staging: pendingStagingBuffers) {
                if (staging.buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(context.GetAllocator(), staging.buffer, staging.allocation);
                    staging.buffer = VK_NULL_HANDLE;
                    staging.allocation = nullptr;
                }
            }
            pendingStagingBuffers.clear();
        }

        bool FlushPendingUploads() {
            if (!transferRecording) return true;

            if (vkEndCommandBuffer(transferCommandBuffer) != VK_SUCCESS) {
                LOG_ERROR("[CTextureManager] Failed to end transfer command buffer");
                transferRecording = false;
                DestroyPendingStagingBuffers();
                return false;
            }

            if (vkResetFences(context.GetDevice(), 1, &transferFence) != VK_SUCCESS) {
                LOG_ERROR("[CTextureManager] Failed to reset transfer fence");
                transferRecording = false;
                DestroyPendingStagingBuffers();
                return false;
            }

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &transferCommandBuffer;
            if (vkQueueSubmit(context.GetGraphicsQueue(), 1, &submitInfo, transferFence) != VK_SUCCESS) {
                LOG_ERROR("[CTextureManager] Failed to submit transfer command buffer");
                transferRecording = false;
                DestroyPendingStagingBuffers();
                return false;
            }

            if (vkWaitForFences(context.GetDevice(), 1, &transferFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
                LOG_ERROR("[CTextureManager] Failed waiting for transfer fence");
                transferRecording = false;
                DestroyPendingStagingBuffers();
                return false;
            }

            transferRecording = false;
            DestroyPendingStagingBuffers();
            return true;
        }
    };

    static constexpr size_t kMaxPendingUploadsBeforeFlush = 32;

    CTextureManager::CTextureManager(const CVulkanContext &ctx, CBindlessAllocator &bindlessAlloc)
        : m_Impl(new Impl_t(ctx, bindlessAlloc)) {
        SetAnisotropy(16.0f);
    }

    void CTextureManager::SetAnisotropy(float maxAnisotropy) const {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_Impl->context.GetPhysicalDevice(), &props);

        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(m_Impl->context.GetPhysicalDevice(), &features);

        const float deviceMax = props.limits.maxSamplerAnisotropy;
        const float requested = std::clamp(maxAnisotropy, 1.0f, deviceMax);
        const bool enable = features.samplerAnisotropy && requested > 1.0f;

        if (m_Impl->sampler != VK_NULL_HANDLE &&
            std::abs(m_Impl->currentAnisotropy - requested) < 0.001f) {
            return;
        }

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.mipLodBias = 0.0f;
        si.anisotropyEnable = enable ? VK_TRUE : VK_FALSE;
        si.maxAnisotropy = enable ? requested : 1.0f;
        si.compareEnable = VK_FALSE;
        si.compareOp = VK_COMPARE_OP_ALWAYS;
        si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        si.unnormalizedCoordinates = VK_FALSE;
        si.minLod = 0.0f;
        si.maxLod = VK_LOD_CLAMP_NONE;

        VkSampler newSampler = VK_NULL_HANDLE;
        if (vkCreateSampler(m_Impl->context.GetDevice(), &si, nullptr, &newSampler) != VK_SUCCESS) {
            if (m_Impl->sampler == VK_NULL_HANDLE)
                throw std::runtime_error("[CTextureManager] Failed to create default sampler");
            LOG_ERROR("[CTextureManager] Failed to rebuild sampler; keeping current");
            return;
        }

        if (m_Impl->sampler != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Impl->context.GetDevice());
            vkDestroySampler(m_Impl->context.GetDevice(), m_Impl->sampler, nullptr);
        }
        m_Impl->sampler = newSampler;
        m_Impl->currentAnisotropy = si.maxAnisotropy;
    }

    void CTextureManager::InitDefaults() const {
        if (m_Impl->whiteTextureId != kInvalidTexture) return;
        constexpr u8 pixels[4] = {255, 0, 255, 255};
        m_Impl->whiteTextureId = Upload(pixels, 1, 1);
    }

    CTextureManager::~CTextureManager() {
        if (!m_Impl->FlushPendingUploads()) {
            LOG_ERROR("[CTextureManager] Failed to flush pending uploads during shutdown");
        }
        m_Impl->DestroyPendingStagingBuffers();

        for (auto &[image, view, allocation]: m_Impl->textures | std::views::values) {
            if (view) vkDestroyImageView(m_Impl->context.GetDevice(), view, nullptr);
            if (image) vmaDestroyImage(m_Impl->context.GetAllocator(), image, allocation);
        }
        m_Impl->textures.clear();

        if (m_Impl->sampler) vkDestroySampler(m_Impl->context.GetDevice(), m_Impl->sampler, nullptr);

        if (m_Impl->transferFence) vkDestroyFence(m_Impl->context.GetDevice(), m_Impl->transferFence, nullptr);
        if (m_Impl->transferCommandPool)
            vkDestroyCommandPool(m_Impl->context.GetDevice(), m_Impl->transferCommandPool, nullptr);

        delete m_Impl;
    }

    VkDescriptorSet CTextureManager::GetBindlessSet() const {
        return m_Impl->bindlessAlloc.GetSet();
    }

    VkDescriptorSetLayout CTextureManager::GetBindlessLayout() const {
        return m_Impl->bindlessAlloc.GetLayout();
    }

    VkSampler CTextureManager::GetSampler() const {
        return m_Impl->sampler;
    }

    TextureHandle CTextureManager::GetWhiteTextureId() const {
        return m_Impl->whiteTextureId;
    }

    void CTextureManager::FlushPendingUploads() const {
        if (!m_Impl->FlushPendingUploads()) {
            throw std::runtime_error("[CTextureManager] Failed to flush pending uploads");
        }
    }

    TextureHandle CTextureManager::Upload(const TextureData_t &data) const {
        if (data.pixels.empty() || data.width <= 0 || data.height <= 0)
            return kInvalidTexture;
        return Upload(data.pixels.data(), data.width, data.height);
    }

    TextureHandle CTextureManager::UploadCubemap(const std::vector<TextureData_t> &faces) const {
        if (faces.size() != 6) {
            LOG_ERROR("[CTextureManager] Cubemap must have exactly 6 faces");
            return kInvalidTexture;
        }

        int width = faces[0].width;
        int height = faces[0].height;
        VkDeviceSize layerSize = static_cast<VkDeviceSize>(width) * height * 4;
        VkDeviceSize totalSize = layerSize * 6;

        VkBuffer stagingBuf{};
        VmaAllocation stagingAlloc{};
        VmaAllocationInfo stagingAllocInfo{};

        VkBufferCreateInfo stagingCI{};
        stagingCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingCI.size = totalSize;
        stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocCI{};
        stagingAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        stagingAllocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if (vmaCreateBuffer(m_Impl->context.GetAllocator(),
                            &stagingCI, &stagingAllocCI,
                            &stagingBuf, &stagingAlloc, &stagingAllocInfo) != VK_SUCCESS) {
            LOG_ERROR("[CTextureManager] Failed to create staging buffer for cubemap");
            return kInvalidTexture;
        }

        u8 *data = static_cast<u8 *>(stagingAllocInfo.pMappedData);
        for (int i = 0; i < 6; ++i) {
            std::memcpy(data + i * layerSize, faces[i].pixels.data(), layerSize);
        }
        vmaFlushAllocation(m_Impl->context.GetAllocator(), stagingAlloc, 0, totalSize);

        VkImageCreateInfo imageCI{};
        imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCI.imageType = VK_IMAGE_TYPE_2D;
        imageCI.extent = {static_cast<u32>(width), static_cast<u32>(height), 1};
        imageCI.mipLevels = 1;
        imageCI.arrayLayers = 6;
        imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaAllocationCreateInfo gpuAllocCI{};
        gpuAllocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        Impl_t::LoadedTexture_t tex{};
        if (vmaCreateImage(m_Impl->context.GetAllocator(), &imageCI, &gpuAllocCI,
                           &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
            LOG_ERROR("[CTextureManager] Failed to create VkImage for cubemap");
            vmaDestroyBuffer(m_Impl->context.GetAllocator(), stagingBuf, stagingAlloc);
            return kInvalidTexture;
        }

        VkImageViewCreateInfo viewCI{};
        viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image = tex.image;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewCI.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};

        if (vkCreateImageView(m_Impl->context.GetDevice(), &viewCI, nullptr, &tex.view) != VK_SUCCESS) {
            LOG_ERROR("[CTextureManager] Failed to create cubemap image view");
            vmaDestroyImage(m_Impl->context.GetAllocator(), tex.image, tex.allocation);
            vmaDestroyBuffer(m_Impl->context.GetAllocator(), stagingBuf, stagingAlloc);
            return kInvalidTexture;
        }

        if (!m_Impl->EnsureTransferRecording()) {
            vkDestroyImageView(m_Impl->context.GetDevice(), tex.view, nullptr);
            vmaDestroyImage(m_Impl->context.GetAllocator(), tex.image, tex.allocation);
            vmaDestroyBuffer(m_Impl->context.GetAllocator(), stagingBuf, stagingAlloc);
            return kInvalidTexture;
        }

        VkCommandBuffer cmd = m_Impl->transferCommandBuffer;
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};

            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy regions[6]{};
            for (int i = 0; i < 6; ++i) {
                regions[i].bufferOffset = i * layerSize;
                regions[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<u32>(i), 1};
                regions[i].imageExtent = {static_cast<u32>(width), static_cast<u32>(height), 1};
            }
            vkCmdCopyBufferToImage(cmd, stagingBuf, tex.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        m_Impl->pendingStagingBuffers.push_back({stagingBuf, stagingAlloc});
        if (m_Impl->pendingStagingBuffers.size() >= kMaxPendingUploadsBeforeFlush) {
            FlushPendingUploads();
        }

        TextureHandle id = m_Impl->nextId++;
        m_Impl->textures.emplace(id, tex);
        m_Impl->bindlessAlloc.UpdateSlot(id, m_Impl->textures[id].view);

        return id;
    }

    TextureHandle CTextureManager::Upload(const u8 *pixels, int width, int height) const {
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

        const u32 mipLevels = static_cast<u32>(
            std::floor(std::log2(std::max(width, height)))) + 1;

        VkFormatProperties formatProps{};
        vkGetPhysicalDeviceFormatProperties(
            m_Impl->context.GetPhysicalDevice(),
            VK_FORMAT_R8G8B8A8_UNORM,
            &formatProps);
        const bool canBlit =
            (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) &&
            (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
            (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
        const u32 finalMipLevels = canBlit ? mipLevels : 1u;

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
            LOG_ERROR("[CTextureManager] Failed to create staging buffer");
            return kInvalidTexture;
        }

        std::memcpy(stagingAllocInfo.pMappedData, pixels, imageSize);
        vmaFlushAllocation(m_Impl->context.GetAllocator(), stagingAlloc, 0, imageSize);

        VkImageCreateInfo imageCI{};
        imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCI.imageType = VK_IMAGE_TYPE_2D;
        imageCI.extent = {static_cast<u32>(width), static_cast<u32>(height), 1};
        imageCI.mipLevels = finalMipLevels;
        imageCI.arrayLayers = 1;
        imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (finalMipLevels > 1) {
            imageCI.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo gpuAllocCI{};
        gpuAllocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        Impl_t::LoadedTexture_t tex{};
        if (vmaCreateImage(m_Impl->context.GetAllocator(), &imageCI, &gpuAllocCI,
                           &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
            LOG_ERROR("[CTextureManager] Failed to create VkImage");
            vmaDestroyBuffer(m_Impl->context.GetAllocator(), stagingBuf, stagingAlloc);
            return kInvalidTexture;
        }

        VkImageViewCreateInfo viewCI{};
        viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image = tex.image;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, finalMipLevels, 0, 1};

        if (vkCreateImageView(m_Impl->context.GetDevice(), &viewCI, nullptr, &tex.view) != VK_SUCCESS) {
            LOG_ERROR("[CTextureManager] Failed to create image view");
            vmaDestroyImage(m_Impl->context.GetAllocator(), tex.image, tex.allocation);
            vmaDestroyBuffer(m_Impl->context.GetAllocator(), stagingBuf, stagingAlloc);
            return kInvalidTexture;
        }

        if (!m_Impl->EnsureTransferRecording()) {
            vkDestroyImageView(m_Impl->context.GetDevice(), tex.view, nullptr);
            vmaDestroyImage(m_Impl->context.GetAllocator(), tex.image, tex.allocation);
            vmaDestroyBuffer(m_Impl->context.GetAllocator(), stagingBuf, stagingAlloc);
            return kInvalidTexture;
        }

        VkCommandBuffer cmd = m_Impl->transferCommandBuffer;
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, finalMipLevels, 0, 1};

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

            if (finalMipLevels > 1) {
                i32 mipWidth = width;
                i32 mipHeight = height;

                for (u32 i = 1; i < finalMipLevels; ++i) {
                    // Transition mip (i-1) -> TRANSFER_SRC to blit from it
                    VkImageMemoryBarrier mipBarrier{};
                    mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    mipBarrier.image = tex.image;
                    mipBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1};
                    mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    mipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    mipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    vkCmdPipelineBarrier(cmd,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

                    VkImageBlit blit{};
                    blit.srcOffsets[0] = {0, 0, 0};
                    blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
                    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
                    blit.dstOffsets[0] = {0, 0, 0};
                    blit.dstOffsets[1] = {
                        mipWidth > 1 ? mipWidth / 2 : 1,
                        mipHeight > 1 ? mipHeight / 2 : 1,
                        1
                    };
                    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};

                    vkCmdBlitImage(cmd,
                                   tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1, &blit, VK_FILTER_LINEAR);

                    // Transition mip (i-1) -> SHADER_READ_ONLY (final).
                    mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(cmd,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                         0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

                    if (mipWidth > 1) mipWidth /= 2;
                    if (mipHeight > 1) mipHeight /= 2;
                }

                // Transition RANSFER_DST -> SHADER_READ_ONLY
                VkImageMemoryBarrier lastBarrier{};
                lastBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                lastBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                lastBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                lastBarrier.image = tex.image;
                lastBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, finalMipLevels - 1, 1, 0, 1};
                lastBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                lastBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                lastBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                lastBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &lastBarrier);
            } else {
                // mip 0 for sampling
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
        }

        m_Impl->pendingStagingBuffers.push_back({stagingBuf, stagingAlloc});
        if (m_Impl->pendingStagingBuffers.size() >= kMaxPendingUploadsBeforeFlush) {
            FlushPendingUploads();
        }

        TextureHandle id = m_Impl->nextId++;
        m_Impl->textures.emplace(id, tex);
        m_Impl->bindlessAlloc.UpdateSlot(id, m_Impl->textures[id].view);

        return id;
    }

    VkImageView CTextureManager::GetView(TextureHandle handle) const {
        auto it = m_Impl->textures.find(handle);
        if (it != m_Impl->textures.end()) return it->second.view;

        auto white = m_Impl->textures.find(m_Impl->whiteTextureId);
        return (white != m_Impl->textures.end()) ? white->second.view : VK_NULL_HANDLE;
    }
} // namespace Manro
