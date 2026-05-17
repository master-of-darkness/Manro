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

        void InitDefaults() const;

        TextureHandle Upload(const TextureData_t &data) const;

        TextureHandle Upload(const u8 *pixels, int width, int height) const;

        TextureHandle UploadCubemap(const std::vector<TextureData_t> &faces) const;

        void FlushPendingUploads() const;

        VkImageView GetView(TextureHandle handle) const;

        VkSampler GetSampler() const;

        void SetAnisotropy(float maxAnisotropy) const;

        TextureHandle GetWhiteTextureId() const;

        VkDescriptorSet GetBindlessSet() const;

        VkDescriptorSetLayout GetBindlessLayout() const;

    private:
        struct Impl_t;
        Scope<Impl_t> m_Impl;
    };
} // namespace Manro
