#pragma once

#include <Manro/Resource/TextureLoader.h>
#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <volk.h>

namespace Manro {
    class VulkanContext;

    class BindlessAllocator;

    class TextureManager {
    public:
        TextureManager(const VulkanContext &ctx, BindlessAllocator &bindlessAlloc);

        ~TextureManager();

        TextureManager(const TextureManager &) = delete;

        TextureManager &operator=(const TextureManager &) = delete;

        void InitDefaults();

        TextureHandle Upload(const TextureData &data);

        TextureHandle Upload(const u8 *pixels, int width, int height);

        TextureHandle UploadCubemap(const std::vector<TextureData> &faces);

        void FlushPendingUploads();

        VkImageView GetView(TextureHandle handle) const;

        VkSampler GetSampler() const;

        TextureHandle GetWhiteTextureId() const;

        VkDescriptorSet GetBindlessSet() const;

        VkDescriptorSetLayout GetBindlessLayout() const;

    private:
        struct Impl;
        Impl *m_Impl;
    };
} // namespace Manro