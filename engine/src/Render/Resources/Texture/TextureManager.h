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

        [[nodiscard]] TextureHandle Upload(const TextureData_t &data) const;

        [[nodiscard]] TextureHandle Upload(const u8 *pixels, int width, int height) const;

        [[nodiscard]] TextureHandle UploadCubemap(const std::vector<TextureData_t> &faces) const;

        void FlushPendingUploads() const;

        [[nodiscard]] VkImageView GetView(TextureHandle handle) const;

        [[nodiscard]] VkSampler GetSampler() const;

        void SetAnisotropy(float maxAnisotropy) const;

        [[nodiscard]] TextureHandle GetWhiteTextureId() const;

        VkDescriptorSet GetBindlessSet() const;

        VkDescriptorSetLayout GetBindlessLayout() const;

    private:
        struct Impl_t;
        Scope<Impl_t> m_Impl;
    };
} // namespace Manro
