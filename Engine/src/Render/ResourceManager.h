#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <Core/Types.h>
#include "Buffer.h"
#include <unordered_map>

namespace Engine {
    class VulkanContext;
    struct ModelData;
    struct PBRSubMeshData;
    struct TextureData;

    struct LoadedMesh {
        Scope<Buffer> vertexBuffer;
        Scope<Buffer> indexBuffer;
        u32 indexCount{0};
    };

    struct LoadedTexture {
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VmaAllocation allocation{nullptr};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    };

    class ResourceManager {
    public:
        void Initialize(const VulkanContext& ctx, VkDescriptorPool pool,
                        VkDescriptorSetLayout texLayout, VkSampler sampler);
        void Shutdown(VkDevice device, VmaAllocator allocator);

        u32 UploadMesh(const ModelData& data);
        u32 UploadPBRMesh(const PBRSubMeshData& data);
        u32 UploadTexture(const TextureData& data);
        u32 UploadTextureRaw(const u8* pixels, int width, int height);

        const LoadedMesh* GetMesh(u32 id) const;
        const LoadedTexture* GetTexture(u32 id) const;

        u32 GetWhiteTextureId() const { return m_WhiteTextureId; }
        u32 GetNeutralNormalTextureId() const { return m_NeutralNormalTextureId; }

        void CreateDefaultTextures();

    private:
        const VulkanContext* m_Context{nullptr};
        VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_TextureLayout{VK_NULL_HANDLE};
        VkSampler m_Sampler{VK_NULL_HANDLE};

        std::unordered_map<u32, LoadedMesh> m_Meshes;
        u32 m_NextMeshId{1};

        std::unordered_map<u32, LoadedTexture> m_Textures;
        u32 m_NextTextureId{1};

        u32 m_WhiteTextureId{0};
        u32 m_NeutralNormalTextureId{0};
    };
} // namespace Engine
