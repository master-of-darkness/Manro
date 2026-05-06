#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Handles.h>
#include <volk.h>
#include <vector>

namespace Manro {
    class CVulkanContext;
    class CPipeline;
    class CBuffer;
    class CTextureManager;
    class CVirtualFS;

    class CSkyboxRenderer {
    public:
        CSkyboxRenderer(CVulkanContext &ctx, CVirtualFS &vfs);

        ~CSkyboxRenderer() = default;

        CSkyboxRenderer(const CSkyboxRenderer &) = delete;

        CSkyboxRenderer &operator=(const CSkyboxRenderer &) = delete;

        void Init(VkDescriptorPool pool, u32 frameCount,
                  VkFormat colorFmt, VkFormat depthFmt, VkSampleCountFlagBits samples);

        void RebuildPipeline(VkFormat colorFmt, VkFormat depthFmt, VkSampleCountFlagBits samples);

        void SetTexture(TextureHandle h, CTextureManager &textures,
                        const std::vector<VkBuffer> &uboBuffers,
                        const std::vector<VkDescriptorSet> &skyboxSets);

        void UpdateDescriptorSet(u32 fi, VkDescriptorSet set,
                                 VkBuffer uboBuffer, CTextureManager &textures);

        void Shutdown();

        bool IsValid() const { return m_SkyboxTexture != kInvalidTexture && m_SkyboxPipeline; }

        VkPipeline GetPipeline() const;

        VkPipelineLayout GetPipelineLayout() const;

        VkBuffer GetVertexBuffer() const;

        VkBuffer GetIndexBuffer() const;

        VkDescriptorSetLayout GetSetLayout() const { return m_SkyboxSetLayout; }
        TextureHandle GetTexture() const { return m_SkyboxTexture; }

    private:
        void BuildPipeline(VkFormat colorFmt, VkFormat depthFmt, VkSampleCountFlagBits samples);

        CVulkanContext &m_Context;
        CVirtualFS &m_Vfs;

        Scope<CPipeline> m_SkyboxPipeline;
        Scope<CBuffer> m_SkyboxVertexBuffer;
        Scope<CBuffer> m_SkyboxIndexBuffer;
        TextureHandle m_SkyboxTexture{kInvalidTexture};

        VkDescriptorSetLayout m_SkyboxSetLayout{VK_NULL_HANDLE};
    };
} // namespace Manro