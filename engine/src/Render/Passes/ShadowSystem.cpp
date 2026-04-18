#include "ShadowSystem.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Pipeline.h"
#include "../Vulkan/Buffer.h"
#include "../Resources/MeshManagerInternal.h"
#include "../../Core/Profiling.h"

#include <Manro/Core/VirtualFS.h>
#include <Manro/Core/Logger.h>
#include <Manro/Render/MeshManager.h>

#include <nvshaders/gltf_scene_io.h.slang>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace Manro {
    struct ShadowMeshInstance_t {
        float modelMatrix[4][4];
        float normalMatrix[3][4];
        u32 materialIndex;
        u32 firstVertex;
        u32 firstIndex;
        u32 indexCount;
        float center[3];
        float radius;
        u32 flags;
        u32 _pad[3];
    };

    struct ShadowDrawCommand_t {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        int vertexOffset;
        u32 firstInstance;
    };

    CShadowSystem::CShadowSystem(CVulkanContext &ctx)
        : m_Context(ctx) {
    }

    void CShadowSystem::Init(VkDescriptorPool pool, const ShadowSettings_t &s,
                             VkDescriptorSetLayout pbrSetLayout) {
        m_bEnabled = s.enabled;
        CreateResources(s);
        BuildMeshCullLayout(pool);
        BuildPipeline(pbrSetLayout);
    }

    void CShadowSystem::Recreate(VkDescriptorPool pool, const ShadowSettings_t &s,
                                 VkDescriptorSetLayout pbrSetLayout,
                                 std::vector<VkDescriptorSet> &pbrSets) {
        vkDeviceWaitIdle(m_Context.GetDevice());

        if (m_ShadowSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_Context.GetDevice(), m_ShadowSampler, nullptr);
            m_ShadowSampler = VK_NULL_HANDLE;
        }
        DestroyImage(m_Context, m_ShadowMap);
        m_ShadowUniformBuffer.reset();

        m_bEnabled = s.enabled;
        CreateResources(s);

        // rebind shadow map into every PBR descriptor set
        for (auto set: pbrSets)
            UpdatePbrDescriptorSetShadow(set);
    }

    void CShadowSystem::Shutdown() {
        VkDevice device = m_Context.GetDevice();

        m_ShadowPipeline.reset();
        m_ShadowUniformBuffer.reset();

        if (m_ShadowSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_ShadowSampler, nullptr);
            m_ShadowSampler = VK_NULL_HANDLE;
        }
        DestroyImage(m_Context, m_ShadowMap);

        if (m_ShadowMeshCullSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_ShadowMeshCullSetLayout, nullptr);
            m_ShadowMeshCullSetLayout = VK_NULL_HANDLE;
        }
    }

    void CShadowSystem::CreateResources(const ShadowSettings_t &s) {
        const u32 shadowMapSize = static_cast<u32>(std::max(128, s.resolution));

        // Newly created shadow map has undefined contents
        m_bShadowMapValid = false;
        m_unFramesSinceShadowUpdate = 0;
        m_LastLightDir = Vec3(0.f);

        {
            ImageCreateParams_t p{};
            p.width = shadowMapSize;
            p.height = shadowMapSize;
            p.format = VK_FORMAT_D32_SFLOAT;
            p.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            p.samples = VK_SAMPLE_COUNT_1_BIT;
            m_ShadowMap = CreateImage(m_Context, p, VK_IMAGE_ASPECT_DEPTH_BIT);
        }

        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_ShadowMap.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        });

        {
            VkSamplerCreateInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            si.magFilter = VK_FILTER_LINEAR;
            si.minFilter = VK_FILTER_LINEAR;
            si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            si.compareEnable = VK_TRUE;
            si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            si.maxLod = 0.f;
            if (vkCreateSampler(m_Context.GetDevice(), &si, nullptr, &m_ShadowSampler) != VK_SUCCESS)
                throw std::runtime_error("Failed to create shadow sampler");
        }

        m_ShadowUniformBuffer = CreateScope<CBuffer>(
            m_Context,
            sizeof(ShadowUniformData_t),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_ShadowUniform.lightDir = Vec4(0.5f, -0.7f, 0.5f, s.bias);
        m_ShadowUniform.shadowMapSize = Vec2(shadowMapSize, shadowMapSize);
        m_ShadowUniform.normalBias = s.bias;
        m_ShadowUniform.softShadows = s.softShadows;
        m_ShadowUniform.shadowsEnabled = s.enabled ? 1 : 0;
    }

    void CShadowSystem::BuildMeshCullLayout(VkDescriptorPool /*pool*/) {
        VkDescriptorSetLayoutBinding b[4];
        b[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        b[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 4;
        ci.pBindings = b;
        if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr,
                                        &m_ShadowMeshCullSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shadow mesh cull descriptor set layout");
    }

    void CShadowSystem::BuildPipeline(VkDescriptorSetLayout pbrSetLayout) {
        auto vertSpv = CVirtualFS::Get().ReadFile("shaders://shadow_depth.vert.spv");
        if (vertSpv.empty()) {
            LOG_ERROR("[CShadowSystem] Shadow depth shader not found");
            return;
        }

        PipelineConfigParams_t cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        cfg.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        cfg.pushConstantSize = sizeof(ShadowPushConstants_t);
        cfg.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
        cfg.descriptorSetLayouts = {pbrSetLayout};

        // Binding 0: Vertex_t, Binding 1: Instance (MeshInstance_t)
        cfg.vertexInputBindings.resize(2);
        cfg.vertexInputBindings[0] = {0, sizeof(float) * 12, VK_VERTEX_INPUT_RATE_VERTEX};
        // Vertex_t: 3+3+2+4 floats = 48 bytes
        cfg.vertexInputBindings[1] = {1, sizeof(ShadowMeshInstance_t), VK_VERTEX_INPUT_RATE_INSTANCE};

        cfg.vertexInputAttributes.resize(9);
        // position (binding 0, loc 0)
        cfg.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        // normal   (binding 0, loc 1)
        cfg.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
        // modelMatrix rows (binding 1, loc 4-7)
        cfg.vertexInputAttributes[2] = {
            4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(ShadowMeshInstance_t, modelMatrix))
        };
        cfg.vertexInputAttributes[3] = {
            5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(ShadowMeshInstance_t, modelMatrix)) + 16
        };
        cfg.vertexInputAttributes[4] = {
            6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(ShadowMeshInstance_t, modelMatrix)) + 32
        };
        cfg.vertexInputAttributes[5] = {
            7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(ShadowMeshInstance_t, modelMatrix)) + 48
        };
        // normalMatrix rows (binding 1, loc 8-10)
        cfg.vertexInputAttributes[6] = {
            8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(ShadowMeshInstance_t, normalMatrix))
        };
        cfg.vertexInputAttributes[7] = {
            9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(ShadowMeshInstance_t, normalMatrix)) + 16
        };
        cfg.vertexInputAttributes[8] = {
            10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(ShadowMeshInstance_t, normalMatrix)) + 32
        };

        m_ShadowPipeline = CreateScope<CPipeline>(m_Context);
        m_ShadowPipeline->BuildShadowDepth(vertSpv, cfg);
    }

    void CShadowSystem::RenderPass(VkCommandBuffer cb,
                                   VkDescriptorSet pbrSet,
                                   VkBuffer instanceBuffer,
                                   u32 totalInstCount,
                                   VkBuffer indexBuffer,
                                   VkBuffer vertexBuffer,
                                   VkBuffer shadowIndirectBuffer,
                                   VkBuffer shadowCountBuffer,
                                   const std::vector<LightData> &pendingLights,
                                   const ShadowSettings_t &s) {
        if (!s.enabled || !m_ShadowPipeline || totalInstCount == 0) return;

        MNR_GPU_ZONE(m_GpuProfileCtx, cb, "Shadow Map");

        const u32 shadowMapSize = static_cast<u32>(std::max(128, s.resolution));

        // Pick directional light direction
        Vec3 lightDir = Vec3(m_ShadowUniform.lightDir);
        for (const auto &l: pendingLights) {
            if (l.type == shaderio::eLightTypeDirectional) {
                lightDir = Vec3(l.direction.x, l.direction.y, l.direction.z);
                break;
            }
        }

        constexpr u32 kForceRefreshFrames = 30;
        const Vec3 normNew = glm::length(lightDir) > 1e-6f ? glm::normalize(lightDir) : Vec3(0.f, -1.f, 0.f);
        const Vec3 normOld = glm::length(m_LastLightDir) > 1e-6f ? glm::normalize(m_LastLightDir) : Vec3(0.f, -1.f, 0.f);
        const float cosDelta = glm::dot(normNew, normOld);
        const bool lightStable = cosDelta > 0.9999999f; // 0.026 degrees
        if (m_bShadowMapValid && lightStable &&
            m_unFramesSinceShadowUpdate < kForceRefreshFrames) {
            ++m_unFramesSinceShadowUpdate;
            return;
        }
        m_LastLightDir = normNew;
        m_unFramesSinceShadowUpdate = 0;
        m_bShadowMapValid = true;

        m_ShadowUniform.lightViewProj = ComputeLightViewProj(lightDir);
        m_ShadowUniform.lightDir = Vec4(lightDir, s.bias);
        m_ShadowUniform.normalBias = s.bias;
        m_ShadowUniform.softShadows = s.softShadows;
        m_ShadowUniform.shadowsEnabled = s.enabled ? 1 : 0;
        m_ShadowUniformBuffer->LoadData(&m_ShadowUniform, sizeof(ShadowUniformData_t));

        // Transition shadow map to attachment write
        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            b.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_ShadowMap.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        VkRenderingAttachmentInfo depthAtt{};
        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAtt.imageView = m_ShadowMap.view;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.clearValue.depthStencil = {1.f, 0};

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent = {shadowMapSize, shadowMapSize};
        ri.layerCount = 1;
        ri.pDepthAttachment = &depthAtt;
        vkCmdBeginRendering(cb, &ri);

        VkViewport vp{
            0.f, 0.f,
            static_cast<float>(shadowMapSize), static_cast<float>(shadowMapSize), 0.f, 1.f
        };
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor{{0, 0}, {shadowMapSize, shadowMapSize}};
        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdSetDepthBias(cb, s.bias, 0.0f, s.slopeBias);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline->GetHandle());
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_ShadowPipeline->GetLayout(), 0, 1, &pbrSet, 0, nullptr);

        ShadowPushConstants_t pc{};
        pc.lightViewProj = m_ShadowUniform.lightViewProj;
        vkCmdPushConstants(cb, m_ShadowPipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        VkBuffer vbufs[2] = {vertexBuffer, instanceBuffer};
        VkDeviceSize offsets[2] = {0, 0};
        vkCmdBindVertexBuffers(cb, 0, 2, vbufs, offsets);
        vkCmdBindIndexBuffer(cb, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexedIndirectCount(cb,
                                      shadowIndirectBuffer, 0,
                                      shadowCountBuffer, 0,
                                      totalInstCount, sizeof(ShadowDrawCommand_t));

        vkCmdEndRendering(cb);

        // Transition back to shader read
        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            b.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_ShadowMap.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
    }

    void CShadowSystem::UpdatePbrDescriptorSetShadow(VkDescriptorSet pbrSet) const {
        VkDescriptorImageInfo shadowImgI{};
        shadowImgI.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowImgI.imageView = m_ShadowMap.view;

        VkDescriptorImageInfo shadowSamplerI{};
        shadowSamplerI.sampler = m_ShadowSampler;

        VkDescriptorBufferInfo shadowUboI{m_ShadowUniformBuffer->GetHandle(), 0, sizeof(ShadowUniformData_t)};

        VkWriteDescriptorSet writes[3]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = pbrSet;
        writes[0].dstBinding = 15;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &shadowImgI;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = pbrSet;
        writes[1].dstBinding = 16;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &shadowSamplerI;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = pbrSet;
        writes[2].dstBinding = 17;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &shadowUboI;

        vkUpdateDescriptorSets(m_Context.GetDevice(), 3, writes, 0, nullptr);
    }

    Mat4 CShadowSystem::ComputeLightViewProj(const Vec3 &lightDir) {
        constexpr float worldRadius = 3500.f;
        constexpr float depth = 10000.f;

        Vec3 normDir = glm::normalize(lightDir);
        Vec3 target = Vec3(0.f, 200.f, 0.f);
        Vec3 lightPos = target - normDir * (depth * 0.5f);
        Vec3 up = (std::abs(normDir.y) > 0.99f)
                      ? Vec3(0.f, 0.f, 1.f)
                      : Vec3(0.f, 1.f, 0.f);

        Mat4 view = glm::lookAt(lightPos, target, up);
        Mat4 proj = glm::ortho(-worldRadius, worldRadius,
                               -worldRadius, worldRadius,
                               0.f, depth);
        proj[1][1] *= -1.f;
        return proj * view;
    }

    VkPipeline CShadowSystem::GetPipeline() const {
        return m_ShadowPipeline ? m_ShadowPipeline->GetHandle() : VK_NULL_HANDLE;
    }

    VkPipelineLayout CShadowSystem::GetPipelineLayout() const {
        return m_ShadowPipeline ? m_ShadowPipeline->GetLayout() : VK_NULL_HANDLE;
    }

    VkBuffer CShadowSystem::GetUniformBufferHandle() const {
        return m_ShadowUniformBuffer ? m_ShadowUniformBuffer->GetHandle() : VK_NULL_HANDLE;
    }
} // namespace Manro