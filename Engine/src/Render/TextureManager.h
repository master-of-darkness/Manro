#pragma once

#include "VulkanHelpers.h"
#include "TextureLoader.h"
#include <Core/Types.h>
#include <unordered_map>

namespace Engine {
    class VulkanContext;

    using TextureHandle = u32;
    inline constexpr TextureHandle kInvalidTexture = 0;

    class TextureManager {
    public:
        TextureManager(const VulkanContext &ctx);

        ~TextureManager();

        TextureManager(const TextureManager &) = delete;

        TextureManager &operator=(const TextureManager &) = delete;

        TextureHandle Upload(const TextureData &data);

        TextureHandle Upload(const u8 *pixels, int width, int height);

        void Bind(TextureHandle handle);

        void ResetBinding();

        VkDescriptorSet GetActiveDescriptorSet() const { return m_ActiveDescriptorSet; }
        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }

    private:
        void CreateDescriptorPool();

        void CreateDescriptorSetLayout();

        void CreateDefaultSampler();

        void CreateWhiteTexture();

        const VulkanContext &m_Context;

        struct LoadedTexture {
            VkImage image{VK_NULL_HANDLE};
            VkImageView view{VK_NULL_HANDLE};
            VmaAllocation allocation{nullptr};
            VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        };

        std::unordered_map<TextureHandle, LoadedTexture> m_Textures;
        TextureHandle m_NextId{1};

        VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_DescriptorSetLayout{VK_NULL_HANDLE};
        VkSampler m_Sampler{VK_NULL_HANDLE};

        TextureHandle m_WhiteTextureId{kInvalidTexture};
        VkDescriptorSet m_ActiveDescriptorSet{VK_NULL_HANDLE};
    };
} // namespace Engine