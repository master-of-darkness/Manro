#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Handles.h>
#include <volk.h>
#include <vector>

namespace Manro {
    class VulkanContext;
    class Pipeline;
    class Buffer;
    class TextureManager;

    class SkyboxRenderer {
    public:
        explicit SkyboxRenderer(VulkanContext &ctx);

        ~SkyboxRenderer() = default;

        SkyboxRenderer(const SkyboxRenderer &) = delete;

        SkyboxRenderer &operator=(const SkyboxRenderer &) = delete;

        void Init(VkDescriptorPool pool, u32 frameCount,
                  VkFormat colorFmt, VkFormat depthFmt, VkSampleCountFlagBits samples);

        void RebuildPipeline(VkFormat colorFmt, VkFormat depthFmt, VkSampleCountFlagBits samples);

        void SetTexture(TextureHandle h, TextureManager &textures,
                        const std::vector<VkBuffer> &uboBuffers,
                        const std::vector<VkDescriptorSet> &skyboxSets);

        void UpdateDescriptorSet(u32 fi, VkDescriptorSet set,
                                 VkBuffer uboBuffer, TextureManager &textures);

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

        VulkanContext &m_Context;

        Scope<Pipeline> m_SkyboxPipeline;
        Scope<Buffer> m_SkyboxVertexBuffer;
        Scope<Buffer> m_SkyboxIndexBuffer;
        TextureHandle m_SkyboxTexture{kInvalidTexture};

        VkDescriptorSetLayout m_SkyboxSetLayout{VK_NULL_HANDLE};
    };
} // namespace Manro