#include "PipelineManager.h"
#include "RendererTypes.h"
#include "MaterialSystem.h"
#include "GpuCullDispatcher.h"
#include "ShadowSystem.h"
#include "SkyboxRenderer.h"
#include "RenderTargetManager.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Pipeline.h"
#include "../Vulkan/Buffer.h"
#include "../Material/Material.h"
#include "../Texture/TextureManager.h"

#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Render/Tonemap/Tonemapper.h>

#include <stdexcept>

namespace Manro {
    PipelineManager::PipelineManager(VulkanContext &ctx)
        : m_Context(ctx) {
    }

    void PipelineManager::CreateDescriptorLayouts() {
        {
            VkDescriptorSetLayoutBinding b[14];
            b[0] = {
                0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };
            b[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[2] = {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[3] = {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[4] = {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[5] = {
                9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };
            b[6] = {10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[7] = {11, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[8] = {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[9] = {13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[10] = {14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[11] = {
                15, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };
            b[12] = {
                16, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };
            b[13] = {
                17, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };

            VkDescriptorBindingFlags flags[14];
            for (unsigned int &flag: flags) flag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

            VkDescriptorSetLayoutBindingFlagsCreateInfo bf{};
            bf.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bf.bindingCount = 14;
            bf.pBindingFlags = flags;

            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 14;
            ci.pBindings = b;
            ci.pNext = &bf;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_PbrSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create PBR descriptor set layout");
        }

        {
            VkDescriptorSetLayoutBinding b{
                0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
            };
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 1;
            ci.pBindings = &b;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_CompositeSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create composite descriptor set layout");
        }
    }

    void PipelineManager::CreateDescriptorPool(u32 frameCount) {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (frameCount * 10)},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (frameCount * 20)},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (frameCount * 10)},
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, (frameCount * 2)},
            {VK_DESCRIPTOR_TYPE_SAMPLER, (frameCount * 10)},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (frameCount * 10)},
        };
        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.poolSizeCount = 6;
        ci.pPoolSizes = sizes;
        ci.maxSets = frameCount * 24;
        if (vkCreateDescriptorPool(m_Context.GetDevice(), &ci, nullptr, &m_DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create descriptor pool");
    }

    void PipelineManager::Shutdown() {
        m_DefaultMaterial.reset();
        m_PbrPipeline.reset();
        m_ZPrepassPipeline.reset();
        m_CompositePipeline.reset();

        if (m_DescriptorPool) {
            vkDestroyDescriptorPool(m_Context.GetDevice(), m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        if (m_PbrSetLayout) {
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_PbrSetLayout, nullptr);
            m_PbrSetLayout = VK_NULL_HANDLE;
        }
        if (m_CompositeSetLayout) {
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_CompositeSetLayout, nullptr);
            m_CompositeSetLayout = VK_NULL_HANDLE;
        }
    }

    void PipelineManager::BuildPbrPipeline(const RenderTargetManager &rt,
                                           const TextureManager &tex,
                                           const RenderSettings &settings) {
        auto vertSpv = VirtualFS::Get().ReadFile("shaders://pbr.vert.spv");
        auto fragSpv = VirtualFS::Get().ReadFile("shaders://pbr.frag.spv");
        auto zPrepassFragSpv = VirtualFS::Get().ReadFile("shaders://pbr_zprepass.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] PBR shaders not found");
            return;
        }
        if (zPrepassFragSpv.empty()) {
            LOG_WARN("[Renderer] Alpha-cutout z-prepass shader not found, masked materials may render opaque.");
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.colorAttachmentFormat = rt.GetOffscreenFormat();
        cfg.depthAttachmentFormat = rt.GetDepthFormat();
        cfg.msaaSamples = ToVulkanSampleCount(settings.msaaSamples);
        cfg.pushConstantSize = sizeof(PBRPushConstants);
        cfg.descriptorSetLayouts = {m_PbrSetLayout, tex.GetBindlessLayout()};

        cfg.vertexInputBindings.resize(2);
        cfg.vertexInputBindings[0] = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        cfg.vertexInputBindings[1] = {1, sizeof(MeshInstance), VK_VERTEX_INPUT_RATE_INSTANCE};

        cfg.vertexInputAttributes.resize(12);
        cfg.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<u32>(offsetof(Vertex, position))};
        cfg.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<u32>(offsetof(Vertex, normal))};
        cfg.vertexInputAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<u32>(offsetof(Vertex, uv))};
        cfg.vertexInputAttributes[3] = {
            3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(Vertex, tangent))
        };
        cfg.vertexInputAttributes[4] = {
            4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            static_cast<u32>(offsetof(MeshInstance, modelMatrix))
        };
        cfg.vertexInputAttributes[5] = {
            5, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            static_cast<u32>(offsetof(MeshInstance, modelMatrix)) + 16
        };
        cfg.vertexInputAttributes[6] = {
            6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(MeshInstance, modelMatrix)) + 32
        };
        cfg.vertexInputAttributes[7] = {
            7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(MeshInstance, modelMatrix)) + 48
        };
        cfg.vertexInputAttributes[8] = {
            8, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            static_cast<u32>(offsetof(MeshInstance, normalMatrix))
        };
        cfg.vertexInputAttributes[9] = {
            9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(MeshInstance, normalMatrix)) + 16
        };
        cfg.vertexInputAttributes[10] = {
            10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(MeshInstance, normalMatrix)) + 32
        };
        cfg.vertexInputAttributes[11] = {
            11, 1, VK_FORMAT_R32_UINT, static_cast<u32>(offsetof(MeshInstance, materialIndex))
        };

        cfg.depthWriteEnable = VK_FALSE;
        cfg.depthCompareOp = VK_COMPARE_OP_EQUAL;

        m_PbrPipeline = CreateScope<Pipeline>(m_Context);
        m_PbrPipeline->BuildGraphics(vertSpv, fragSpv, cfg);

        PipelineConfigParams zCfg = cfg;
        zCfg.fragmentEntryPoint = zPrepassFragSpv.empty() ? "" : "main";
        zCfg.colorAttachmentFormat = VK_FORMAT_UNDEFINED;
        zCfg.depthWriteEnable = VK_TRUE;
        zCfg.depthCompareOp = VK_COMPARE_OP_LESS;

        m_ZPrepassPipeline = CreateScope<Pipeline>(m_Context);
        m_ZPrepassPipeline->BuildGraphics(vertSpv, zPrepassFragSpv, zCfg);

        VkDescriptorSetLayoutBinding stub{
            0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
        };
        VkDescriptorSetLayoutCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dci.bindingCount = 1;
        dci.pBindings = &stub;
        VkDescriptorSetLayout stubLayout;
        vkCreateDescriptorSetLayout(m_Context.GetDevice(), &dci, nullptr, &stubLayout);
        m_DefaultMaterial = CreateRef<Material>(m_Context, nullptr, stubLayout);
    }

    void PipelineManager::BuildCompositePipeline(VkFormat swapchainFormat) {
        auto vertSpv = VirtualFS::Get().ReadFile("shaders://composite.vert.spv");
        auto fragSpv = VirtualFS::Get().ReadFile("shaders://composite.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] Composite shaders not found");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.colorAttachmentFormat = swapchainFormat;
        cfg.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        cfg.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        cfg.pushConstantSize = sizeof(CompositePushConstants);
        cfg.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;
        cfg.descriptorSetLayouts = {m_CompositeSetLayout};

        m_CompositePipeline = CreateScope<Pipeline>(m_Context);
        m_CompositePipeline->BuildGraphics(vertSpv, fragSpv, cfg);
    }

    void PipelineManager::UpdatePbrDescriptorSet(u32 fi, FrameData &frame,
                                                 const MaterialSystem &matSys,
                                                 TextureManager &tex,
                                                 const ShadowSystem &shadow,
                                                 SkyboxRenderer &skybox) {
        VkDescriptorBufferInfo uboI{frame.uboBuffer->GetHandle(), 0, sizeof(UniformBufferObject)};
        VkDescriptorBufferInfo lightI{frame.lightBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo tileHI{frame.tileHeaderBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo tileLI{frame.tileLightIndexBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo instI{frame.instanceBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo matI{matSys.GetMaterialBufferHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo texI{matSys.GetTextureInfoBufferHandle(), 0, VK_WHOLE_SIZE};

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = tex.GetSampler();

        VkDescriptorImageInfo stubImg{};
        stubImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        stubImg.imageView = tex.GetView(tex.GetWhiteTextureId());

        VkWriteDescriptorSet writes[9]{};
        auto w = [&](int i, VkDescriptorSet set, u32 binding, VkDescriptorType type,
                     const VkDescriptorBufferInfo *bi = nullptr, const VkDescriptorImageInfo *ii = nullptr) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = binding;
            writes[i].descriptorType = type;
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = bi;
            writes[i].pImageInfo = ii;
        };
        w(0, frame.pbrSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &uboI);
        w(1, frame.pbrSet, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lightI);
        w(2, frame.pbrSet, 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tileHI);
        w(3, frame.pbrSet, 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tileLI);
        w(4, frame.pbrSet, 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instI);
        w(5, frame.pbrSet, 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &matI);
        w(6, frame.pbrSet, 1, VK_DESCRIPTOR_TYPE_SAMPLER, nullptr, &samplerInfo);
        w(7, frame.pbrSet, 10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &stubImg);
        w(8, frame.pbrSet, 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &texI);
        vkUpdateDescriptorSets(m_Context.GetDevice(), 9, writes, 0, nullptr);

        // Cull set
        VkWriteDescriptorSet cw[4]{};
        auto cull = [&](int i, u32 binding, VkDescriptorType type, const VkDescriptorBufferInfo *bi) {
            cw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cw[i].dstSet = frame.cullSet;
            cw[i].dstBinding = binding;
            cw[i].descriptorType = type;
            cw[i].descriptorCount = 1;
            cw[i].pBufferInfo = bi;
        };
        cull(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lightI);
        cull(1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tileHI);
        cull(2, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tileLI);
        cull(3, 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &uboI);
        vkUpdateDescriptorSets(m_Context.GetDevice(), 4, cw, 0, nullptr);

        // Mesh-cull set
        VkDescriptorBufferInfo cullDataI{frame.cullDataBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo indirectI{frame.indirectBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo countI{frame.countBuffer->GetHandle(), 0, VK_WHOLE_SIZE};

        VkWriteDescriptorSet mw[4]{};
        auto mc = [&](int i, u32 binding, const VkDescriptorBufferInfo *bi) {
            mw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mw[i].dstSet = frame.meshCullSet;
            mw[i].dstBinding = binding;
            mw[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            mw[i].descriptorCount = 1;
            mw[i].pBufferInfo = bi;
        };
        mc(0, 0, &cullDataI);
        mc(1, 1, &indirectI);
        mc(2, 2, &countI);
        mc(3, 3, &instI);
        vkUpdateDescriptorSets(m_Context.GetDevice(), 4, mw, 0, nullptr);

        VkDescriptorBufferInfo shadowIndirectI{frame.shadowIndirectBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo shadowCountI{frame.shadowCountBuffer->GetHandle(), 0, VK_WHOLE_SIZE};

        VkWriteDescriptorSet sw[4]{};
        auto sc = [&](int i, u32 binding, const VkDescriptorBufferInfo *bi) {
            sw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            sw[i].dstSet = frame.shadowMeshCullSet;
            sw[i].dstBinding = binding;
            sw[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            sw[i].descriptorCount = 1;
            sw[i].pBufferInfo = bi;
        };
        sc(0, 0, &cullDataI);
        sc(1, 1, &shadowIndirectI);
        sc(2, 2, &shadowCountI);
        sc(3, 3, &instI);
        vkUpdateDescriptorSets(m_Context.GetDevice(), 4, sw, 0, nullptr);

        shadow.UpdatePbrDescriptorSetShadow(frame.pbrSet);
        UpdateSkyboxDescriptorSet(fi, frame, skybox, tex);
    }

    void PipelineManager::UpdateCompositeDescriptorSet(u32 fi, FrameData &frame,
                                                       const RenderTargetManager &rt) {
        VkDescriptorImageInfo imgI{};
        imgI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgI.imageView = rt.GetOffscreenView();
        imgI.sampler = rt.GetOffscreenSampler();

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = frame.compositeSet;
        w.dstBinding = 0;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo = &imgI;
        vkUpdateDescriptorSets(m_Context.GetDevice(), 1, &w, 0, nullptr);
    }

    void PipelineManager::UpdateSkyboxDescriptorSet(u32 fi, FrameData &frame,
                                                    SkyboxRenderer &skybox,
                                                    TextureManager &tex) {
        if (skybox.GetTexture() == kInvalidTexture) return;
        skybox.UpdateDescriptorSet(fi, frame.skyboxSet,
                                   frame.uboBuffer->GetHandle(), tex);
    }

    void PipelineManager::AllocateFrameDescriptorSets(FrameData &frame,
                                                      const GpuCullDispatcher &cull,
                                                      const ShadowSystem &shadow,
                                                      const SkyboxRenderer &skybox) {
        VkDescriptorSetLayout layouts[5] = {
            m_PbrSetLayout, m_CompositeSetLayout,
            cull.GetCullSetLayout(), cull.GetMeshCullSetLayout(),
            shadow.GetMeshCullSetLayout()
        };
        VkDescriptorSet sets[5];
        VkDescriptorSetAllocateInfo dsAI{};
        dsAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAI.descriptorPool = m_DescriptorPool;
        dsAI.descriptorSetCount = 5;
        dsAI.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(m_Context.GetDevice(), &dsAI, sets) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate descriptor sets");
        frame.pbrSet = sets[0];
        frame.compositeSet = sets[1];
        frame.cullSet = sets[2];
        frame.meshCullSet = sets[3];
        frame.shadowMeshCullSet = sets[4];

        VkDescriptorSetLayout skyLayout = skybox.GetSetLayout();
        VkDescriptorSetAllocateInfo skyAI{};
        skyAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        skyAI.descriptorPool = m_DescriptorPool;
        skyAI.descriptorSetCount = 1;
        skyAI.pSetLayouts = &skyLayout;
        vkAllocateDescriptorSets(m_Context.GetDevice(), &skyAI, &frame.skyboxSet);
    }
} // namespace Manro