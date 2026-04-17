#pragma once

#include <Manro/Resource/TextureLoader.h>
#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <volk.h>

namespace Manro {
    class CVulkanContext;

    class CBindlessAllocator;

    class CTextureManager {
    public:
        CTextureManager(const CVulkanContext &ctx, CBindlessAllocator &bindlessAlloc);

        ~CTextureManager();

        CTextureManager(const CTextureManager &) = delete;

        CTextureManager &operator=(const CTextureManager &) = delete;

        void InitDefaults();

        TextureHandle Upload(const TextureData_t &data);

        TextureHandle Upload(const u8 *pixels, int width, int height);

        TextureHandle UploadCubemap(const std::vector<TextureData_t> &faces);

        void FlushPendingUploads();

        VkImageView GetView(TextureHandle handle) const;

        VkSampler GetSampler() const;

        void SetAnisotropy(float maxAnisotropy);

        TextureHandle GetWhiteTextureId() const;

        VkDescriptorSet GetBindlessSet() const;

        VkDescriptorSetLayout GetBindlessLayout() const;

    private:
        struct Impl_t;
        Impl_t *m_Impl;
    };
} // namespace Manro
