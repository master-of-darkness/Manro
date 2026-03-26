#pragma once

#include <Manro/Render/RHI/ICommandList.h>
#include <volk.h>
#include <span>
#include <unordered_map>

namespace Manro::RHI {

    struct VulkanTextureBinding {
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
    };

    struct VulkanZPrepassState {
        VkExtent2D extent{};
        VkImageView depthView{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSets[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
        u32 descriptorSetCount{0};
        VkBuffer indexBuffer{VK_NULL_HANDLE};
        VkBuffer vertexBuffers[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkDeviceSize vertexOffsets[2]{0, 0};
        VkBuffer indirectBuffer{VK_NULL_HANDLE};
        VkBuffer countBuffer{VK_NULL_HANDLE};
        u32 instanceCount{0};
        u32 drawStride{0};
    };

    struct VulkanPbrPassState {
        VkExtent2D extent{};
        VkClearValue clearColor{};
        VkSampleCountFlagBits msaaSamples{VK_SAMPLE_COUNT_1_BIT};
        VkImageView msaaColorView{VK_NULL_HANDLE};
        VkImageView offscreenColorView{VK_NULL_HANDLE};
        VkImageView depthView{VK_NULL_HANDLE};

        VkPipeline pipeline{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSets[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
        u32 descriptorSetCount{0};

        VkBuffer indexBuffer{VK_NULL_HANDLE};
        VkBuffer vertexBuffers[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkDeviceSize vertexOffsets[2]{0, 0};
        VkBuffer indirectBuffer{VK_NULL_HANDLE};
        VkBuffer countBuffer{VK_NULL_HANDLE};
        u32 instanceCount{0};
        u32 drawStride{0};
    };

    struct VulkanCompositePassState {
        VkExtent2D extent{};
        VkImageView colorView{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        const void *pushConstants{nullptr};
        u32 pushConstantSize{0};
        VkShaderStageFlags pushConstantStages{VK_SHADER_STAGE_FRAGMENT_BIT};
    };

    class VulkanCommandList final : public ICommandList {
    public:
        VulkanCommandList() = default;

        void SetCommandBuffer(VkCommandBuffer cmd) { m_CommandBuffer = cmd; }

        void ImportBuffer(BufferHandle handle, VkBuffer buffer);

        void ImportTexture(TextureHandle handle, VulkanTextureBinding texture);

        void ImportGraphicsPipeline(PipelineHandle handle, VkPipeline pipeline, VkPipelineLayout layout);

        void ExecuteZPrepass(const VulkanZPrepassState &state);

        void ExecutePbrPass(const VulkanPbrPassState &state);

        void ExecuteCompositePass(const VulkanCompositePassState &state);

        void BeginRendering(std::span<const ColorAttachment> color, const DepthAttachment *depth = nullptr) override;

        void EndRendering() override;

        void SetViewport(const Viewport &vp) override;

        void SetScissor(const Scissor &sc) override;

        void BindPipeline(PipelineHandle pso) override;

        void SetDepthBias(float constant, float clamp, float slope) override;

        void BindVertexBuffer(u32 slot, BufferHandle buf, u64 offset = 0) override;

        void BindIndexBuffer(BufferHandle buf, u64 offset = 0) override;

        void BindDescriptorSets(u32 firstSet, const void **sets, u32 setCount) override;

        void PushConstants(const void *data, u32 size, u32 offset = 0) override;

        void DrawIndexed(u32 indexCount, u32 instanceCount = 1,
                         u32 firstIndex = 0, i32 vertexOffset = 0,
                         u32 firstInstance = 0) override;

        void DrawIndexedIndirectCount(BufferHandle indirectBuf, u64 indirectOffset,
                                      BufferHandle countBuf, u64 countOffset,
                                      u32 maxDrawCount, u32 stride) override;

        void Dispatch(u32 x, u32 y, u32 z) override;

        void TextureBarrier(TextureHandle tex,
                            u32 srcStageMask, u32 srcAccessMask,
                            u32 dstStageMask, u32 dstAccessMask,
                            u32 oldLayout, u32 newLayout) override;

        void BufferBarrier(BufferHandle buf,
                           u32 srcStageMask, u32 srcAccessMask,
                           u32 dstStageMask, u32 dstAccessMask) override;

        void FillBuffer(BufferHandle buf, u64 offset, u64 size, u32 value) override;

        void BeginDebugLabel(const char *name) override;

        void EndDebugLabel() override;

    private:
        struct VulkanPipelineBinding {
            VkPipeline pipeline{VK_NULL_HANDLE};
            VkPipelineLayout layout{VK_NULL_HANDLE};
            VkPipelineBindPoint bindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};
        };

        VkCommandBuffer m_CommandBuffer{VK_NULL_HANDLE};
        VkPipelineLayout m_CurrentLayout{VK_NULL_HANDLE};
        VkPipelineBindPoint m_CurrentBindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};

        std::unordered_map<u32, VkBuffer> m_Buffers;
        std::unordered_map<u32, VulkanTextureBinding> m_Textures;
        std::unordered_map<u32, VulkanPipelineBinding> m_Pipelines;
    };

} // namespace Manro::RHI

