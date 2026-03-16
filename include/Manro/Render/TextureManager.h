#pragma once

#include <Manro/Render/Vulkan/VulkanHelpers.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Core/Types.h>
#include <unordered_map>

namespace Manro {
    class VulkanContext;

    using TextureHandle = u32;
    inline constexpr TextureHandle kInvalidTexture = 0xFFFFFFFF;

    class TextureManager {
    public:
        TextureManager(const VulkanContext &ctx);

        ~TextureManager();

        TextureManager(const TextureManager &) = delete;

        TextureManager &operator=(const TextureManager &) = delete;

        TextureHandle Upload(const TextureData &data);

        TextureHandle Upload(const u8 *pixels, int width, int height);

        void ResetBinding();

        VkImageView GetView(TextureHandle handle) const;
        VkSampler GetSampler() const { return m_Sampler; }
        TextureHandle GetWhiteTextureId() const { return m_WhiteTextureId; }

        VkDescriptorSet GetBindlessSet() const { return m_BindlessSet; }
        VkDescriptorSetLayout GetBindlessLayout() const { return m_BindlessLayout; }

    private:
        void CreateDefaultSampler();
        void CreateWhiteTexture();
        void CreateBindlessResources();
        void UpdateBindlessSet(u32 index, VkImageView view);

        const VulkanContext &m_Context;

        struct LoadedTexture {
            VkImage image{VK_NULL_HANDLE};
            VkImageView view{VK_NULL_HANDLE};
            VmaAllocation allocation{nullptr};
        };

        std::unordered_map<TextureHandle, LoadedTexture> m_Textures;
        TextureHandle m_NextId{0}; // Start from 0 for easy indexing

        VkSampler m_Sampler{VK_NULL_HANDLE};
        TextureHandle m_WhiteTextureId{kInvalidTexture};

        VkDescriptorSetLayout m_BindlessLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_BindlessPool{VK_NULL_HANDLE};
        VkDescriptorSet m_BindlessSet{VK_NULL_HANDLE};

        static constexpr u32 kMaxBindlessTextures = 2048;
    };
} // namespace Manro
