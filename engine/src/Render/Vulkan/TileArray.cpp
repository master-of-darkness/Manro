#include "TileArray.h"
#include "VulkanContext.h"
#include "VulkanHelpers.h"
#include "Buffer.h"

#include <Manro/Core/Logger.h>
#include <stdexcept>
#include <vector>

namespace Manro {
    namespace {
        constexpr u32 kTileMips = 5;

        u32 FindTileMemory(VkPhysicalDevice phys, u32 bits) {
            VkPhysicalDeviceMemoryProperties props{};
            vkGetPhysicalDeviceMemoryProperties(phys, &props);
            for (u32 i = 0; i < props.memoryTypeCount; ++i) {
                if ((bits & (1u << i)) &&
                    (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                    return i;
            }
            throw std::runtime_error("[CTileArray] No device-local memory type");
        }
    }

    CTileArray::CTileArray(CVulkanContext &ctx) : m_Context(ctx) {
    }

    CTileArray::~CTileArray() {
        Clear();
    }

    void CTileArray::CreateDescriptor() {
        DestroyDescriptor();
        VkDevice device = m_Context.GetDevice();
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1;
        li.pBindings = &b;
        if (vkCreateDescriptorSetLayout(device, &li, nullptr, &m_TileSetLayout) != VK_SUCCESS)
            throw std::runtime_error("[CTileArray] Tile set layout failed");
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &ps;
        if (vkCreateDescriptorPool(device, &pi, nullptr, &m_TilePool) != VK_SUCCESS)
            throw std::runtime_error("[CTileArray] Tile pool failed");
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_TilePool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_TileSetLayout;
        if (vkAllocateDescriptorSets(device, &ai, &m_TileSet) != VK_SUCCESS)
            throw std::runtime_error("[CTileArray] Tile set alloc failed");
    }

    void CTileArray::DestroyDescriptor() {
        VkDevice device = m_Context.GetDevice();
        if (!device)
            return;
        m_TileSet = VK_NULL_HANDLE;
        if (m_TilePool) {
            vkDestroyDescriptorPool(device, m_TilePool, nullptr);
            m_TilePool = VK_NULL_HANDLE;
        }
        if (m_TileSetLayout) {
            vkDestroyDescriptorSetLayout(device, m_TileSetLayout, nullptr);
            m_TileSetLayout = VK_NULL_HANDLE;
        }
    }

    void CTileArray::DestroyImage() {
        VkDevice device = m_Context.GetDevice();
        if (!device)
            return;
        if (m_TileSampler) {
            vkDestroySampler(device, m_TileSampler, nullptr);
            m_TileSampler = VK_NULL_HANDLE;
        }
        if (m_TileView) {
            vkDestroyImageView(device, m_TileView, nullptr);
            m_TileView = VK_NULL_HANDLE;
        }
        if (m_TileImage) {
            vkDestroyImage(device, m_TileImage, nullptr);
            m_TileImage = VK_NULL_HANDLE;
        }
        if (m_TileMemory) {
            vkFreeMemory(device, m_TileMemory, nullptr);
            m_TileMemory = VK_NULL_HANDLE;
        }
        m_Layers = 0;
    }

    void CTileArray::Clear() {
        DestroyImage();
        DestroyDescriptor();
    }

    void CTileArray::Ensure(u32 layers) {
        if (layers == 0) layers = 1;
        if (m_TileImage && m_Layers == layers && m_TileSetLayout)
            return;
        DestroyImage();
        CreateDescriptor();
        VkDevice device = m_Context.GetDevice();
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_SRGB;
        ii.extent = {16, 16, 1};
        ii.mipLevels = kTileMips;
        ii.arrayLayers = layers;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &ii, nullptr, &m_TileImage) != VK_SUCCESS)
            throw std::runtime_error("[CTileArray] Tile image failed");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, m_TileImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindTileMemory(m_Context.GetPhysicalDevice(), req.memoryTypeBits);
        if (vkAllocateMemory(device, &ai, nullptr, &m_TileMemory) != VK_SUCCESS)
            throw std::runtime_error("[CTileArray] Tile memory failed");
        vkBindImageMemory(device, m_TileImage, m_TileMemory, 0);
        m_Layers = layers;

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_TileImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format = VK_FORMAT_R8G8B8A8_SRGB;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kTileMips, 0, layers};
        if (vkCreateImageView(device, &vi, nullptr, &m_TileView) != VK_SUCCESS)
            throw std::runtime_error("[CTileArray] Tile view failed");

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.minLod = 0.f;
        si.maxLod = static_cast<float>(kTileMips - 1);
        si.maxAnisotropy = 1.f;
        if (vkCreateSampler(device, &si, nullptr, &m_TileSampler) != VK_SUCCESS)
            throw std::runtime_error("[CTileArray] Tile sampler failed");
    }

    void CTileArray::Upload(u32 layers, const u8 *bytes, u64 byteSize, u64 layerBytes) {
        if (layers == 0 || !bytes || byteSize == 0) return;
        Ensure(layers);
        CBuffer stageBuf(m_Context, byteSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        stageBuf.LoadData(bytes, static_cast<size_t>(byteSize));

        std::vector<VkBufferImageCopy> regions;
        regions.reserve(static_cast<size_t>(layers) * kTileMips);
        for (u32 l = 0; l < layers; ++l) {
            VkDeviceSize off = static_cast<VkDeviceSize>(layerBytes) * l;
            u32 dim = 16;
            for (u32 m = 0; m < kTileMips; ++m) {
                VkBufferImageCopy r{};
                r.bufferOffset = off;
                r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, l, 1};
                r.imageExtent = {dim, dim, 1};
                regions.push_back(r);
                off += static_cast<VkDeviceSize>(dim) * dim * 4u;
                dim /= 2u;
            }
        }

        VkImage tileImage = m_TileImage;
        u32 regionCount = static_cast<u32>(regions.size());
        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b0{};
            b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b0.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b0.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b0.image = tileImage;
            b0.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kTileMips, 0, layers};
            b0.srcAccessMask = 0;
            b0.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b0);
            vkCmdCopyBufferToImage(cmd, stageBuf.GetHandle(), tileImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   regionCount, regions.data());
            VkImageMemoryBarrier b1 = b0;
            b1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b1.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                                 1, &b1);
        });

        VkDescriptorImageInfo ii2{};
        ii2.sampler = m_TileSampler;
        ii2.imageView = m_TileView;
        ii2.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_TileSet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii2;
        vkUpdateDescriptorSets(m_Context.GetDevice(), 1, &w, 0, nullptr);
        LOG_INFO("[CTileArray] Tile array: {} layers, {} mips", layers, kTileMips);
    }

    void CTileArray::AppendTo(PipelineConfigParams_t &cfg) {
        if (m_TileSetLayout != VK_NULL_HANDLE)
            cfg.descriptorSetLayouts = {m_TileSetLayout};
    }
}
