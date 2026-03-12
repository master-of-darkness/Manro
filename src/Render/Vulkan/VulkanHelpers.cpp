#include <Manro/Render/Vulkan/VulkanHelpers.h>
#include <volk.h>
#include <Manro/Render/Vulkan/VulkanContext.h>
#include <Manro/Core/Logger.h>
#include <stdexcept>
#include <fstream>
#include <vector>

namespace Manro {
    AllocatedImage CreateImage(const VulkanContext &ctx, const ImageCreateParams &params,
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

        AllocatedImage result{};
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

    void DestroyImage(const VulkanContext &ctx, AllocatedImage &img) {
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

    void ExecuteOneShot(const VulkanContext &ctx, const OneShotWork &work) {
        VkCommandPool tmpPool{};
        VkCommandBuffer tmpCmd{};

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = ctx.GetGraphicsQueueFamilyIndex();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkCreateCommandPool(ctx.GetDevice(), &poolInfo, nullptr, &tmpPool);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = tmpPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(ctx.GetDevice(), &allocInfo, &tmpCmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(tmpCmd, &beginInfo);

        work(tmpCmd);

        vkEndCommandBuffer(tmpCmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &tmpCmd;
        vkQueueSubmit(ctx.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(ctx.GetGraphicsQueue());

        vkDestroyCommandPool(ctx.GetDevice(), tmpPool, nullptr);
    }

    std::vector<u8> ReadBinaryFile(const std::string &filepath) {
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            return {};
        }

        size_t fileSize = (size_t) file.tellg();
        std::vector<u8> buffer(fileSize);
        file.seekg(0);
        file.read((char *) buffer.data(), fileSize);
        file.close();

        return buffer;
    }
} // namespace Manro
