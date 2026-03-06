#include "ResourceManager.h"
#include "VulkanContext.h"
#include "ModelLoader.h"
#include "TextureLoader.h"
#include <Core/Logger.h>
#include <cstring>
#include <stdexcept>

namespace Engine {
    void ResourceManager::Initialize(const VulkanContext& ctx, VkDescriptorPool pool,
                                     VkDescriptorSetLayout texLayout, VkSampler sampler) {
        m_Context = &ctx;
        m_DescriptorPool = pool;
        m_TextureLayout = texLayout;
        m_Sampler = sampler;
    }

    void ResourceManager::Shutdown(VkDevice device, VmaAllocator allocator) {
        for (auto& [id, tex] : m_Textures) {
            if (tex.view) vkDestroyImageView(device, tex.view, nullptr);
            if (tex.image) vmaDestroyImage(allocator, tex.image, tex.allocation);
        }
        m_Textures.clear();
        m_Meshes.clear();
    }

    void ResourceManager::CreateDefaultTextures() {
        const u8 whitePixel[4] = {255, 255, 255, 255};
        m_WhiteTextureId = UploadTextureRaw(whitePixel, 1, 1);

        const u8 normalPixel[4] = {128, 128, 255, 255};
        m_NeutralNormalTextureId = UploadTextureRaw(normalPixel, 1, 1);
    }

    u32 ResourceManager::UploadTextureRaw(const u8* pixels, int width, int height) {
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

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

        if (vmaCreateBuffer(m_Context->GetAllocator(), &stagingBufInfo, &stagingAllocInfo,
                            &stagingBuf, &stagingAlloc, &stagingAllocRes) != VK_SUCCESS) {
            LOG_ERROR("[ResourceManager] Failed to create texture staging buffer!");
            return 0;
        }
        std::memcpy(stagingAllocRes.pMappedData, pixels, imageSize);

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
        if (vmaCreateImage(m_Context->GetAllocator(), &imageInfo, &gpuAllocInfo,
                           &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
            LOG_ERROR("[ResourceManager] Failed to create VkImage!");
            vmaDestroyBuffer(m_Context->GetAllocator(), stagingBuf, stagingAlloc);
            return 0;
        }

        VkCommandPool tmpPool{};
        VkCommandBuffer tmpCmd{};

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_Context->GetGraphicsQueueFamilyIndex();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkCreateCommandPool(m_Context->GetDevice(), &poolInfo, nullptr, &tmpPool);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = tmpPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &allocInfo, &tmpCmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(tmpCmd, &beginInfo);

        {
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
            vkCmdPipelineBarrier(tmpCmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<u32>(width), static_cast<u32>(height), 1};
        vkCmdCopyBufferToImage(tmpCmd, stagingBuf, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(tmpCmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        vkEndCommandBuffer(tmpCmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &tmpCmd;
        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkDestroyCommandPool(m_Context->GetDevice(), tmpPool, nullptr);
        vmaDestroyBuffer(m_Context->GetAllocator(), stagingBuf, stagingAlloc);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = tex.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        if (vkCreateImageView(m_Context->GetDevice(), &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
            LOG_ERROR("[ResourceManager] Failed to create texture image view!");
            return 0;
        }

        VkDescriptorSetAllocateInfo dsAllocInfo{};
        dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAllocInfo.descriptorPool = m_DescriptorPool;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &m_TextureLayout;
        vkAllocateDescriptorSets(m_Context->GetDevice(), &dsAllocInfo, &tex.descriptorSet);

        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfo.imageView = tex.view;
        imgInfo.sampler = m_Sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = tex.descriptorSet;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imgInfo;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 1, &write, 0, nullptr);

        u32 id = m_NextTextureId++;
        m_Textures.emplace(id, std::move(tex));
        return id;
    }

    u32 ResourceManager::UploadTexture(const TextureData& data) {
        if (data.pixels.empty() || data.width <= 0 || data.height <= 0) return 0;
        return UploadTextureRaw(data.pixels.data(), data.width, data.height);
    }

    u32 ResourceManager::UploadMesh(const ModelData& data) {
        if (data.vertices.empty() || data.indices.empty()) return 0;

        LoadedMesh mesh;
        mesh.indexCount = static_cast<u32>(data.indices.size());

        mesh.vertexBuffer = CreateScope<Buffer>(*m_Context,
                                                sizeof(ModelVertex) * data.vertices.size(),
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        mesh.vertexBuffer->LoadData(data.vertices.data(), sizeof(ModelVertex) * data.vertices.size());

        mesh.indexBuffer = CreateScope<Buffer>(*m_Context,
                                               sizeof(u32) * data.indices.size(),
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        mesh.indexBuffer->LoadData(data.indices.data(), sizeof(u32) * data.indices.size());

        u32 id = m_NextMeshId++;
        m_Meshes.emplace(id, std::move(mesh));
        return id;
    }

    u32 ResourceManager::UploadPBRMesh(const PBRSubMeshData& data) {
        if (data.vertices.empty() || data.indices.empty()) return 0;

        LoadedMesh mesh;
        mesh.indexCount = static_cast<u32>(data.indices.size());

        mesh.vertexBuffer = CreateScope<Buffer>(*m_Context,
                                                sizeof(PBRVertex) * data.vertices.size(),
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
        mesh.vertexBuffer->LoadData(data.vertices.data(),
                                    sizeof(PBRVertex) * data.vertices.size());

        mesh.indexBuffer = CreateScope<Buffer>(*m_Context,
                                               sizeof(u32) * data.indices.size(),
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                               VMA_MEMORY_USAGE_CPU_TO_GPU);
        mesh.indexBuffer->LoadData(data.indices.data(),
                                   sizeof(u32) * data.indices.size());

        u32 id = m_NextMeshId++;
        m_Meshes.emplace(id, std::move(mesh));
        return id;
    }

    const LoadedMesh* ResourceManager::GetMesh(u32 id) const {
        auto it = m_Meshes.find(id);
        return it != m_Meshes.end() ? &it->second : nullptr;
    }

    const LoadedTexture* ResourceManager::GetTexture(u32 id) const {
        auto it = m_Textures.find(id);
        return it != m_Textures.end() ? &it->second : nullptr;
    }
} // namespace Engine
