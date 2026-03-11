#include <Manro/Render/Vulkan/Pipeline.h>
#include <Manro/Render/Vulkan/VulkanContext.h>
#include <iostream>
#include <stdexcept>

namespace Engine {
    Pipeline::Pipeline(const VulkanContext &context) : m_Context(context) {
    }

    Pipeline::~Pipeline() {
        Shutdown();
    }

    void Pipeline::BuildGraphics(const std::vector<u8> &vertexSpv,
                                 const std::vector<u8> &fragmentSpv,
                                 const PipelineConfigParams &config) {
        VkShaderModule vertModule = CreateShaderModule(vertexSpv);
        VkShaderModule fragModule = CreateShaderModule(fragmentSpv);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = config.vertexEntryPoint.c_str();
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = config.fragmentEntryPoint.c_str();

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = static_cast<u32>(config.vertexInputBindings.size());
        vertexInput.pVertexBindingDescriptions = config.vertexInputBindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(config.vertexInputAttributes.size());
        vertexInput.pVertexAttributeDescriptions = config.vertexInputAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = config.msaaSamples;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.logicOpEnable = VK_FALSE;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = config.pushConstantSize;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        if (config.descriptorSetLayout != VK_NULL_HANDLE) {
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &config.descriptorSetLayout;
        }
        if (config.pushConstantSize > 0) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
        }

        if (vkCreatePipelineLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create pipeline layout!");

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &config.colorAttachmentFormat;
        renderingInfo.depthAttachmentFormat = config.depthAttachmentFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(m_Context.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) !=
            VK_SUCCESS)
            throw std::runtime_error("Failed to create graphics pipeline!");

        vkDestroyShaderModule(m_Context.GetDevice(), fragModule, nullptr);
        vkDestroyShaderModule(m_Context.GetDevice(), vertModule, nullptr);
    }

    void Pipeline::Shutdown() {
        if (m_Pipeline) {
            vkDestroyPipeline(m_Context.GetDevice(), m_Pipeline, nullptr);
            m_Pipeline = VK_NULL_HANDLE;
        }
        if (m_PipelineLayout) {
            vkDestroyPipelineLayout(m_Context.GetDevice(), m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
        }
    }

    VkShaderModule Pipeline::CreateShaderModule(const std::vector<u8> &spvCode) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = spvCode.size();
        info.pCode = reinterpret_cast<const uint32_t *>(spvCode.data());

        VkShaderModule module;
        if (vkCreateShaderModule(m_Context.GetDevice(), &info, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shader module!");
        return module;
    }
} // namespace Engine
