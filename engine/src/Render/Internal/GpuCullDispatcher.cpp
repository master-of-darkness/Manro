#include "GpuCullDispatcher.h"
#include "ShadowSystem.h"
#include "RenderMathUtils.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Pipeline.h"
#include "../Vulkan/PipelineCache.h"
#include "../Core/Profiling.h"

#include <Manro/Core/VirtualFS.h>
#include <Manro/Core/Logger.h>
#include <Manro/Render/MeshManager.h>

namespace Manro {
    CGpuCullDispatcher::CGpuCullDispatcher(CVulkanContext &ctx)
        : m_Context(ctx) {
    }

    void CGpuCullDispatcher::Init() {
        // Cull set layout
        {
            VkDescriptorSetLayoutBinding b[4];
            b[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 4;
            ci.pBindings = b;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_CullSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create cull descriptor set layout");
        }

        // Mesh cull set layout
        {
            VkDescriptorSetLayoutBinding b[4];
            b[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 4;
            ci.pBindings = b;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_MeshCullSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create mesh cull descriptor set layout");
        }
    }

    void CGpuCullDispatcher::Shutdown() {
        m_CullPipeline.reset();
        m_MeshCullPipeline.reset();
        if (m_CullSetLayout) {
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_CullSetLayout, nullptr);
            m_CullSetLayout = VK_NULL_HANDLE;
        }
        if (m_MeshCullSetLayout) {
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_MeshCullSetLayout, nullptr);
            m_MeshCullSetLayout = VK_NULL_HANDLE;
        }
    }

    void CGpuCullDispatcher::BuildPipelines(CPipelineCache &cache) {
        // Light tile culling pipeline
        {
            auto compSpv = CVirtualFS::Get().ReadFile("shaders://forward_plus_cull.comp.spv");
            if (compSpv.empty()) {
                LOG_ERROR("[CRenderer] Cull shader not found");
                return;
            }

            PipelineConfigParams_t cfg{};
            cfg.computeEntryPoint = "main";
            cfg.pushConstantSize = 176;
            cfg.descriptorSetLayouts = {m_CullSetLayout};

            PipelineKey_t key{};
            key.compHash = CPipelineCache::HashSpirV(compSpv);
            key.variants = PipelineVariant_Compute;
            key.pushConstantSize = 176;
            VkDescriptorSetLayout layouts[] = {m_CullSetLayout};
            key.setLayoutHash = CPipelineCache::HashLayouts(layouts, 1);

            m_CullPipeline = CreateScope<CPipeline>(m_Context);
            cache.GetCompute(key, [&](VkPipelineCache) -> VkPipeline {
                m_CullPipeline->BuildCompute(compSpv, cfg);
                return m_CullPipeline->GetHandle();
            });
        }

        // Mesh culling pipeline
        {
            auto compSpv = CVirtualFS::Get().ReadFile("shaders://mesh_cull.comp.spv");
            if (compSpv.empty()) {
                LOG_ERROR("[CRenderer] Mesh cull shader not found");
                return;
            }

            PipelineConfigParams_t cfg{};
            cfg.computeEntryPoint = "main";
            cfg.pushConstantSize = sizeof(MeshCullPushConstants_t);
            cfg.descriptorSetLayouts = {m_MeshCullSetLayout};

            PipelineKey_t key{};
            key.compHash = CPipelineCache::HashSpirV(compSpv);
            key.variants = PipelineVariant_Compute;
            key.pushConstantSize = sizeof(MeshCullPushConstants_t);
            VkDescriptorSetLayout layouts[] = {m_MeshCullSetLayout};
            key.setLayoutHash = CPipelineCache::HashLayouts(layouts, 1);

            m_MeshCullPipeline = CreateScope<CPipeline>(m_Context);
            cache.GetCompute(key, [&](VkPipelineCache) -> VkPipeline {
                m_MeshCullPipeline->BuildCompute(compSpv, cfg);
                return m_MeshCullPipeline->GetHandle();
            });
        }
    }

    void CGpuCullDispatcher::Dispatch(const DispatchParams_t &params) {
        VkCommandBuffer cb = params.cb;
        FrameData_t &frame = params.frame;

        vkCmdFillBuffer(cb, frame.countBuffer->GetHandle(), 0, sizeof(u32), 0);

        VkBufferMemoryBarrier2 fillBarrier{};
        fillBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        fillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        fillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        fillBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        fillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        fillBarrier.buffer = frame.countBuffer->GetHandle();
        fillBarrier.offset = 0;
        fillBarrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo fillDep{};
        fillDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        fillDep.bufferMemoryBarrierCount = 1;
        fillDep.pBufferMemoryBarriers = &fillBarrier;
        vkCmdPipelineBarrier2(cb, &fillDep);

        // Mesh frustum culling
        {
            MNR_GPU_ZONE(m_GpuProfileCtx, cb, "Mesh Frustum Cull");
            DispatchMeshCull(cb, frame, params.totalInstCount,
                             params.projectionMatrix * params.viewMatrix,
                             params.cameraPosition, params.settings);
        }

        // Shadow culling + shadow render pass
        if (params.settings.shadows.enabled) {
            MNR_GPU_ZONE(m_GpuProfileCtx, cb, "Shadow Cull + Render");
            DispatchShadowCull(cb, frame, params.totalInstCount,
                               params.shadow, params.lights,
                               params.cameraPosition, params.settings,
                               params.meshes);
        }

        // Light tile culling
        {
            MNR_GPU_ZONE(m_GpuProfileCtx, cb, "Light Tile Cull");
            DispatchLightTileCull(cb, frame,
                                  params.viewMatrix, params.projectionMatrix,
                                  params.lights, params.renderExtent,
                                  params.maxLightsPerTile, params.maxTilesX, params.maxTilesY,
                                  params.tileSize, params.settings);
        }
    }

    void CGpuCullDispatcher::DispatchMeshCull(VkCommandBuffer cb, FrameData_t &frame,
                                              u32 totalInstCount, const Mat4 &viewProj,
                                              const Vec3 &cameraPosition, const RenderSettings_t &settings) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_MeshCullPipeline->GetHandle());
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_MeshCullPipeline->GetLayout(), 0, 1, &frame.meshCullSet, 0, nullptr);

        MeshCullPushConstants_t mcpc{};
        Mat4 vp = viewProj;
        ExtractFrustumPlanes(vp, mcpc.planes);
        mcpc.instanceCount = totalInstCount;
        mcpc.cameraPos = Vec4(cameraPosition, 1.0f);
        mcpc.maxDrawDistance = settings.maxDrawDistance;
        mcpc.enableFrustumCulling = settings.enableFrustumCulling ? 1u : 0u;

        vkCmdPushConstants(cb, m_MeshCullPipeline->GetLayout(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(MeshCullPushConstants_t), &mcpc);

        u32 groupCount = (mcpc.instanceCount + 63) / 64;
        vkCmdDispatch(cb, groupCount, 1, 1);

        VkBufferMemoryBarrier2 meshCullBarriers[3]{};
        meshCullBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        meshCullBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        meshCullBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        meshCullBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        meshCullBarriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        meshCullBarriers[0].buffer = frame.indirectBuffer->GetHandle();
        meshCullBarriers[0].offset = 0;
        meshCullBarriers[0].size = VK_WHOLE_SIZE;
        meshCullBarriers[1] = meshCullBarriers[0];
        meshCullBarriers[1].buffer = frame.countBuffer->GetHandle();
        meshCullBarriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        meshCullBarriers[2].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        meshCullBarriers[2].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        meshCullBarriers[2].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        meshCullBarriers[2].dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        meshCullBarriers[2].buffer = frame.instanceBuffer->GetHandle();
        meshCullBarriers[2].offset = 0;
        meshCullBarriers[2].size = VK_WHOLE_SIZE;
        VkDependencyInfo meshDep{};
        meshDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        meshDep.bufferMemoryBarrierCount = 3;
        meshDep.pBufferMemoryBarriers = meshCullBarriers;
        vkCmdPipelineBarrier2(cb, &meshDep);
    }

    void CGpuCullDispatcher::DispatchShadowCull(VkCommandBuffer cb, FrameData_t &frame,
                                                u32 totalInstCount, CShadowSystem &shadow,
                                                const std::vector<LightData> &lights,
                                                const Vec3 &cameraPosition, const RenderSettings_t &settings,
                                                CMeshManager &meshes) {
        vkCmdFillBuffer(cb, frame.shadowCountBuffer->GetHandle(), 0, sizeof(u32), 0);

        VkBufferMemoryBarrier2 shadowFillBarrier{};
        shadowFillBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        shadowFillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        shadowFillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        shadowFillBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        shadowFillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        shadowFillBarrier.buffer = frame.shadowCountBuffer->GetHandle();
        shadowFillBarrier.offset = 0;
        shadowFillBarrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo shadowFillDep{};
        shadowFillDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        shadowFillDep.bufferMemoryBarrierCount = 1;
        shadowFillDep.pBufferMemoryBarriers = &shadowFillBarrier;
        vkCmdPipelineBarrier2(cb, &shadowFillDep);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_MeshCullPipeline->GetHandle());
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_MeshCullPipeline->GetLayout(), 0, 1,
                                &frame.shadowMeshCullSet, 0, nullptr);

        {
            Vec3 lightDir = Vec3(shadow.GetUniform().lightDir);
            for (const auto &l: lights)
                if (l.type == shaderio::eLightTypeDirectional) {
                    lightDir = Vec3(l.direction.x, l.direction.y, l.direction.z);
                    break;
                }
            Mat4 shadowVP = CShadowSystem::ComputeLightViewProj(lightDir);

            MeshCullPushConstants_t shadowPc{};
            ExtractFrustumPlanes(shadowVP, shadowPc.planes);
            shadowPc.instanceCount = totalInstCount;
            shadowPc.cameraPos = Vec4(cameraPosition, 1.0f);
            shadowPc.maxDrawDistance = settings.maxDrawDistance;
            shadowPc.enableFrustumCulling = settings.enableFrustumCulling ? 1u : 0u;

            vkCmdPushConstants(cb, m_MeshCullPipeline->GetLayout(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(shadowPc), &shadowPc);
            vkCmdDispatch(cb, (totalInstCount + 63) / 64, 1, 1);
        }

        VkBufferMemoryBarrier2 shadowCullBarriers[2]{};
        shadowCullBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        shadowCullBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        shadowCullBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        shadowCullBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        shadowCullBarriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        shadowCullBarriers[0].buffer = frame.shadowIndirectBuffer->GetHandle();
        shadowCullBarriers[0].offset = 0;
        shadowCullBarriers[0].size = VK_WHOLE_SIZE;
        shadowCullBarriers[1] = shadowCullBarriers[0];
        shadowCullBarriers[1].buffer = frame.shadowCountBuffer->GetHandle();
        VkDependencyInfo shadowCullDep{};
        shadowCullDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        shadowCullDep.bufferMemoryBarrierCount = 2;
        shadowCullDep.pBufferMemoryBarriers = shadowCullBarriers;
        vkCmdPipelineBarrier2(cb, &shadowCullDep);

        // Shadow render pass
        shadow.RenderPass(cb,
                          frame.pbrSet,
                          frame.instanceBuffer->GetHandle(),
                          totalInstCount,
                          meshes.GetIndexBuffer()->GetHandle(),
                          meshes.GetVertexBuffer()->GetHandle(),
                          frame.shadowIndirectBuffer->GetHandle(),
                          frame.shadowCountBuffer->GetHandle(),
                          lights,
                          settings.shadows);
    }

    void CGpuCullDispatcher::DispatchLightTileCull(VkCommandBuffer cb, FrameData_t &frame,
                                                   const Mat4 &viewMatrix, const Mat4 &projectionMatrix,
                                                  const std::vector<LightData> &lights,
                                                  VkExtent2D renderExtent,
                                                  u32 maxLightsPerTile, u32 maxTilesX, u32 maxTilesY, u32 tileSize,
                                                   const RenderSettings_t &settings) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline->GetHandle());
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_CullPipeline->GetLayout(), 0, 1, &frame.cullSet, 0, nullptr);

        struct CullPushConstants_t {
            Mat4 view;
            Mat4 proj;
            Vec4 screenTile;
            u32 lightCount;
            u32 maxPerTile;
            u32 tilesX;
            u32 tilesY;
            Vec4 zParams;
        } cpc{};

        cpc.view = viewMatrix;
        cpc.proj = projectionMatrix;
        cpc.proj[1][1] *= -1;
        cpc.screenTile = Vec4(static_cast<float>(renderExtent.width), static_cast<float>(renderExtent.height),
                              static_cast<float>(tileSize), static_cast<float>(tileSize));
        cpc.lightCount = static_cast<u32>(lights.size());
        cpc.maxPerTile = maxLightsPerTile;
        cpc.tilesX = std::min((renderExtent.width + tileSize - 1) / tileSize, maxTilesX);
        cpc.tilesY = std::min((renderExtent.height + tileSize - 1) / tileSize, maxTilesY);
        cpc.zParams = Vec4(settings.nearZ, settings.farZ, 1.f, 0.f);

        vkCmdPushConstants(cb, m_CullPipeline->GetLayout(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cpc), &cpc);
        vkCmdDispatch(cb, cpc.tilesX, cpc.tilesY, 1);

        VkMemoryBarrier2 cullMemBarrier{};
        cullMemBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        cullMemBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        cullMemBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        cullMemBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        cullMemBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        VkDependencyInfo cullDep{};
        cullDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        cullDep.memoryBarrierCount = 1;
        cullDep.pMemoryBarriers = &cullMemBarrier;
        vkCmdPipelineBarrier2(cb, &cullDep);
    }
} // namespace Manro