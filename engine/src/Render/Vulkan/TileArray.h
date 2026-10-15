#pragma once

#include "Pipeline.h"
#include <volk.h>

namespace Manro {
    class CVulkanContext;
    class CBuffer;

    class CTileArray {
    public:
        explicit CTileArray(CVulkanContext &ctx);
        ~CTileArray();

        CTileArray(const CTileArray &) = delete;
        CTileArray &operator=(const CTileArray &) = delete;

        void Ensure(u32 layers);
        void Upload(u32 layers, const u8 *bytes, u64 byteSize, u64 layerBytes);
        void Clear();
        void AppendTo(PipelineConfigParams_t &cfg);

        [[nodiscard]] u32 Layers() const { return m_Layers; }
        [[nodiscard]] bool Valid() const { return m_TileSet != VK_NULL_HANDLE; }
        [[nodiscard]] VkDescriptorSet NativeSet() const { return m_TileSet; }

    private:
        void DestroyImage();
        void DestroyDescriptor();
        void CreateDescriptor();

        CVulkanContext &m_Context;
        VkImage m_TileImage{VK_NULL_HANDLE};
        VkDeviceMemory m_TileMemory{VK_NULL_HANDLE};
        VkImageView m_TileView{VK_NULL_HANDLE};
        VkSampler m_TileSampler{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_TileSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_TilePool{VK_NULL_HANDLE};
        VkDescriptorSet m_TileSet{VK_NULL_HANDLE};
        u32 m_Layers{0};
    };
}
