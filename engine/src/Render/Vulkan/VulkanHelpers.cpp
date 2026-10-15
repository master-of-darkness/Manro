#include "VulkanHelpers.h"
#include "VulkanContext.h"
#include "Buffer.h"
#include "SparseBuffer.h"

#include <volk.h>
#include <Manro/Core/Logger.h>
#include <stdexcept>
#include <fstream>
#include <vector>
#include <cstring>

namespace Manro {
    AllocatedImage_t CreateImage(const CVulkanContext &ctx, const ImageCreateParams_t &params,
                                 VkImageAspectFlags aspect) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {params.width, params.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = params.format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = params.usage;
        imageInfo.samples = params.samples;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        AllocatedImage_t result{};
        if (vmaCreateImage(ctx.GetAllocator(), &imageInfo, &allocInfo,
                           &result.image, &result.allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanHelpers] Failed to create image!");
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = result.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = params.format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(ctx.GetDevice(), &viewInfo, nullptr, &result.view) != VK_SUCCESS) {
            vmaDestroyImage(ctx.GetAllocator(), result.image, result.allocation);
            throw std::runtime_error("[VulkanHelpers] Failed to create image view!");
        }

        return result;
    }

    void DestroyImage(const CVulkanContext &ctx, AllocatedImage_t &img) {
        if (img.view) {
            vkDestroyImageView(ctx.GetDevice(), img.view, nullptr);
            img.view = VK_NULL_HANDLE;
        }
        if (img.image) {
            vmaDestroyImage(ctx.GetAllocator(), img.image, img.allocation);
            img.image = VK_NULL_HANDLE;
            img.allocation = nullptr;
        }
    }

    void ExecuteOneShot(const CVulkanContext &ctx, const OneShotWork &work) {
        VkDevice device = ctx.GetDevice();
        VkCommandPool commandPool = ctx.GetOneShotCommandPool();
        VkCommandBuffer commandBuffer = ctx.GetOneShotCommandBuffer();
        VkFence fence = ctx.GetOneShotFence();

        if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanHelpers] Failed to wait for one-shot fence");
        }
        if (vkResetFences(device, 1, &fence) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanHelpers] Failed to reset one-shot fence");
        }
        if (vkResetCommandPool(device, commandPool, 0) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanHelpers] Failed to reset one-shot command pool");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanHelpers] Failed to begin one-shot command buffer");
        }

        work(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanHelpers] Failed to end one-shot command buffer");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        if (vkQueueSubmit(ctx.GetGraphicsQueue(), 1, &submitInfo, fence) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanHelpers] Failed to submit one-shot command buffer");
        }
        if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanHelpers] Failed waiting for one-shot submission");
        }
    }

    std::vector<u8> ReadBinaryFile(const std::string &filepath) {
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("[VulkanHelpers] Failed to open file: {}", filepath);
            return {};
        }

        const size_t fileSize = file.tellg();
        std::vector<u8> buffer(fileSize);
        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
        file.close();

        return buffer;
    }

    void FillSparseSync(const CVulkanContext &ctx, CSparseBuffer &dst, u64 offset, u64 size) {
        if (size == 0) return;
        VkBuffer native = dst.NativeBuffer();
        ExecuteOneShot(ctx, [&](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, native, static_cast<VkDeviceSize>(offset),
                            static_cast<VkDeviceSize>(size), 0);
        });
    }

    void CopyBufferToSparseSync(const CVulkanContext &ctx, const CBuffer &staging, CSparseBuffer &dst,
                                const BufferCopyRegion *regions, u32 count) {
        if (count == 0 || !regions) return;
        std::vector<VkBufferCopy> copies(count);
        for (u32 i = 0; i < count; ++i) {
            copies[i].srcOffset = static_cast<VkDeviceSize>(regions[i].srcOffset);
            copies[i].dstOffset = static_cast<VkDeviceSize>(regions[i].dstOffset);
            copies[i].size = static_cast<VkDeviceSize>(regions[i].size);
        }
        VkBuffer src = staging.GetHandle();
        VkBuffer dstBuf = dst.NativeBuffer();
        ExecuteOneShot(ctx, [&](VkCommandBuffer cmd) {
            vkCmdCopyBuffer(cmd, src, dstBuf, count, copies.data());
        });
    }

    void UploadBytesToSparseSync(const CVulkanContext &ctx, const void *bytes, u64 byteSize,
                                 CSparseBuffer &dst, u64 dstOffset) {
        if (!bytes || byteSize == 0) return;
        CBuffer staging(ctx, byteSize, EBufferUsage::TransferSrc, EBufferMemory::CpuToGpu);
        staging.LoadData(bytes, static_cast<size_t>(byteSize));
        BufferCopyRegion region{};
        region.srcOffset = 0;
        region.dstOffset = dstOffset;
        region.size = byteSize;
        CopyBufferToSparseSync(ctx, staging, dst, &region, 1);
    }
} // namespace Manro
