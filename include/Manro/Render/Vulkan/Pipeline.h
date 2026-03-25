#pragma once
#include <volk.h>
#include <Manro/Core/Types.h>
#include <vector>

namespace Manro {
    class VulkanContext;

    struct PipelineConfigParams {
        VkFormat colorAttachmentFormat{VK_FORMAT_UNDEFINED};
        VkFormat depthAttachmentFormat{VK_FORMAT_UNDEFINED};

        std::vector<VkVertexInputBindingDescription> vertexInputBindings;
        std::vector<VkVertexInputAttributeDescription> vertexInputAttributes;

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        VkSampleCountFlagBits msaaSamples{VK_SAMPLE_COUNT_1_BIT};

        u32 pushConstantSize{0};
        VkShaderStageFlags pushConstantStages{0};
        std::string vertexEntryPoint{"main"};
        std::string fragmentEntryPoint{"main"};
        std::string computeEntryPoint{"main"};

        VkBool32 depthWriteEnable{VK_TRUE};
        VkCompareOp depthCompareOp{VK_COMPARE_OP_LESS};
        VkColorComponentFlags colorWriteMask{
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT};
    };

    class Pipeline {
    public:
        explicit Pipeline(const VulkanContext &context);

        ~Pipeline();

        void BuildGraphics(const std::vector<u8> &vertexSpv,
                           const std::vector<u8> &fragmentSpv,
                           const PipelineConfigParams &config);

        void BuildCompute(const std::vector<u8> &computeSpv,
                          const PipelineConfigParams &config);

        void BuildShadowDepth(const std::vector<u8> &vertexSpv,
                              const PipelineConfigParams &config);

        void Shutdown();

        VkPipeline GetHandle() const { return m_Pipeline; }
        VkPipelineLayout GetLayout() const { return m_PipelineLayout; }

    private:
        VkShaderModule CreateShaderModule(const std::vector<u8> &spvCode);

        const VulkanContext &m_Context;
        VkPipeline m_Pipeline{VK_NULL_HANDLE};
        VkPipelineLayout m_PipelineLayout{VK_NULL_HANDLE};
    };
} // namespace Manro
