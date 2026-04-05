#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>

namespace Manro::Internal {
    struct ZPrepassPassState {
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

    struct PbrPassState {
        VkExtent2D extent{};
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

    struct CompositePassState {
        VkExtent2D extent{};
        VkImageView colorView{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        const void *pushConstants{nullptr};
        u32 pushConstantSize{0};
        VkShaderStageFlags pushConstantStages{VK_SHADER_STAGE_FRAGMENT_BIT};
    };

    struct SkyboxPassState {
        VkExtent2D extent{};
        VkImageView offscreenColorView{VK_NULL_HANDLE};
        VkImageView msaaColorView{VK_NULL_HANDLE};
        VkSampleCountFlagBits msaaSamples{VK_SAMPLE_COUNT_1_BIT};
        VkImageView depthView{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        VkBuffer vertexBuffer{VK_NULL_HANDLE};
        VkBuffer indexBuffer{VK_NULL_HANDLE};
        u32 indexCount{0};
    };
} // namespace Manro::Internal
