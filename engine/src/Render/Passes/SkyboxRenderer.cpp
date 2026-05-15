#include "SkyboxRenderer.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Pipeline.h"
#include "../Vulkan/Buffer.h"
#include "../Resources/Texture/TextureManager.h"

#include <Manro/Core/VirtualFS.h>
#include <Manro/Core/Logger.h>
#include <stdexcept>

namespace Manro {
    CSkyboxRenderer::CSkyboxRenderer(CVulkanContext &ctx, CVirtualFS &vfs)
        : m_Context(ctx), m_Vfs(vfs) {
    }

    void CSkyboxRenderer::Init(VkDescriptorPool pool, u32 frameCount,
                               VkFormat colorFmt, VkFormat depthFmt,
                              VkSampleCountFlagBits samples) {
        VkDevice device = m_Context.GetDevice();

        VkDescriptorSetLayoutBinding b[2];
        b[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 2;
        ci.pBindings = b;
        if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &m_SkyboxSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create skybox descriptor set layout");

        float skyboxVertices[] = {
            -5000.0f, 5000.0f, -5000.0f, -5000.0f, -5000.0f, -5000.0f, 5000.0f, -5000.0f, -5000.0f,
            5000.0f, -5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, -5000.0f, 5000.0f, -5000.0f,
            -5000.0f, -5000.0f, 5000.0f, -5000.0f, -5000.0f, -5000.0f, -5000.0f, 5000.0f, -5000.0f,
            -5000.0f, 5000.0f, -5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, -5000.0f, 5000.0f,
            5000.0f, -5000.0f, -5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, 5000.0f, 5000.0f,
            5000.0f, 5000.0f, 5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, -5000.0f, -5000.0f,
            -5000.0f, -5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, 5000.0f, 5000.0f, 5000.0f,
            5000.0f, 5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, -5000.0f, -5000.0f, 5000.0f,
            -5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, 5000.0f,
            5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, -5000.0f,
            -5000.0f, -5000.0f, -5000.0f, -5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, -5000.0f,
            5000.0f, -5000.0f, -5000.0f, -5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f
        };
        u32 skyboxIndices[36];
        for (u32 i = 0; i < 36; ++i) skyboxIndices[i] = i;

        m_SkyboxVertexBuffer = CreateScope<CBuffer>(m_Context, sizeof(skyboxVertices),
                                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                   VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_SkyboxVertexBuffer->LoadData(skyboxVertices, sizeof(skyboxVertices));

        m_SkyboxIndexBuffer = CreateScope<CBuffer>(m_Context, sizeof(skyboxIndices),
                                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                  VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_SkyboxIndexBuffer->LoadData(skyboxIndices, sizeof(skyboxIndices));

        BuildPipeline(colorFmt, depthFmt, samples);
    }

    void CSkyboxRenderer::RebuildPipeline(VkFormat colorFmt, VkFormat depthFmt,
                                          VkSampleCountFlagBits samples) {
        BuildPipeline(colorFmt, depthFmt, samples);
    }

    void CSkyboxRenderer::BuildPipeline(VkFormat colorFmt, VkFormat depthFmt,
                                        VkSampleCountFlagBits samples) {
        auto vertSpv = m_Vfs.ReadFile("shaders://skybox.vert.spv");
        auto fragSpv = m_Vfs.ReadFile("shaders://skybox.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[CSkyboxRenderer] Skybox shaders not found");
            return;
        }

        PipelineConfigParams_t cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.colorAttachmentFormat = colorFmt;
        cfg.depthAttachmentFormat = depthFmt;
        cfg.msaaSamples = samples;
        cfg.descriptorSetLayouts = {m_SkyboxSetLayout};
        cfg.depthWriteEnable = VK_FALSE;
        cfg.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        cfg.vertexInputBindings.resize(1);
        cfg.vertexInputBindings[0] = {0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX};
        cfg.vertexInputAttributes.resize(1);
        cfg.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};

        m_SkyboxPipeline = CreateScope<CPipeline>(m_Context);
        m_SkyboxPipeline->BuildGraphics(vertSpv, fragSpv, cfg);
    }

    void CSkyboxRenderer::SetTexture(TextureHandle h, const CTextureManager &textures,
                                     const std::vector<VkBuffer> &uboBuffers,
                                    const std::vector<VkDescriptorSet> &skyboxSets) {
        if (h == kInvalidTexture)
            LOG_ERROR("[CSkyboxRenderer] SetTexture called with invalid texture!");

        m_SkyboxTexture = h;

        for (u32 i = 0; i < static_cast<u32>(skyboxSets.size()); ++i) {
            UpdateDescriptorSet(i, skyboxSets[i], uboBuffers[i], textures);
        }
    }

    void CSkyboxRenderer::ClearTexture() {
        m_SkyboxTexture = kInvalidTexture;
    }

    void CSkyboxRenderer::UpdateDescriptorSet(u32 /*fi*/, VkDescriptorSet set,
                                              VkBuffer uboBuffer, const CTextureManager &textures) const {
        VkDescriptorBufferInfo uboI{uboBuffer, 0, VK_WHOLE_SIZE};

        VkDescriptorImageInfo skyI{};
        skyI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        skyI.imageView = textures.GetView(m_SkyboxTexture);
        skyI.sampler = textures.GetSampler();

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &uboI;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &skyI;

        vkUpdateDescriptorSets(m_Context.GetDevice(), 2, writes, 0, nullptr);
    }

    void CSkyboxRenderer::Shutdown() {
        VkDevice device = m_Context.GetDevice();

        m_SkyboxPipeline.reset();
        m_SkyboxVertexBuffer.reset();
        m_SkyboxIndexBuffer.reset();

        if (m_SkyboxSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_SkyboxSetLayout, nullptr);
            m_SkyboxSetLayout = VK_NULL_HANDLE;
        }
    }

    VkPipeline CSkyboxRenderer::GetPipeline() const {
        return m_SkyboxPipeline ? m_SkyboxPipeline->GetHandle() : VK_NULL_HANDLE;
    }

    VkPipelineLayout CSkyboxRenderer::GetPipelineLayout() const {
        return m_SkyboxPipeline ? m_SkyboxPipeline->GetLayout() : VK_NULL_HANDLE;
    }

    VkBuffer CSkyboxRenderer::GetVertexBuffer() const {
        return m_SkyboxVertexBuffer ? m_SkyboxVertexBuffer->GetHandle() : VK_NULL_HANDLE;
    }

    VkBuffer CSkyboxRenderer::GetIndexBuffer() const {
        return m_SkyboxIndexBuffer ? m_SkyboxIndexBuffer->GetHandle() : VK_NULL_HANDLE;
    }
} // namespace Manro
