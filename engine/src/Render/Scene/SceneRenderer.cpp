#include "SceneRenderer.h"
#include "../Internal/PassStates.h"
#include "../../Core/Profiling.h"

namespace Manro {
    CSceneRenderer::CSceneRenderer() = default;

    void CSceneRenderer::SetZPrepassState(const Internal::ZPrepassPassState_t *state) {
        m_ZPrepassState = state;
    }

    void CSceneRenderer::SetPbrPassState(const Internal::PbrPassState_t *state) {
        m_PbrPassState = state;
    }

    void CSceneRenderer::SetSkyboxPassState(const Internal::SkyboxPassState_t *state) {
        m_SkyboxPassState = state;
    }

    void CSceneRenderer::SetCompositePassState(const Internal::CompositePassState_t *state) {
        m_CompositePassState = state;
    }

    void CSceneRenderer::Flush(VkCommandBuffer cmd) {
        if (!cmd) {
            return;
        }

        if (m_ZPrepassState && m_ZPrepassState
            ->
            depthView
        ) {
            MNR_GPU_ZONE(m_GpuProfileCtx, cmd, "Z-Prepass");
            const auto &state = *m_ZPrepassState;
            VkRenderingAttachmentInfo depthAtt{};
            depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAtt.imageView = state.depthView;
            depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAtt.clearValue.depthStencil = {1.f, 0};

            VkRenderingInfo ri{};
            ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            ri.renderArea.extent = state.extent;
            ri.layerCount = 1;
            ri.colorAttachmentCount = 0;
            ri.pDepthAttachment = &depthAtt;
            vkCmdBeginRendering(cmd, &ri);

            if (state.pipeline && state.indexBuffer && state.indirectBuffer && state.countBuffer) {
                VkViewport vp{
                    0.f, 0.f, static_cast<float>(state.extent.width), static_cast<float>(state.extent.height),
                    0.f, 1.f
                };
                vkCmdSetViewport(cmd, 0, 1, &vp);
                VkRect2D sc{{0, 0}, state.extent};
                vkCmdSetScissor(cmd, 0, 1, &sc);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
                if (state.pipelineLayout && state.descriptorSetCount > 0) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            state.pipelineLayout, 0, state.descriptorSetCount,
                                            state.descriptorSets, 0, nullptr);
                }
                vkCmdBindIndexBuffer(cmd, state.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindVertexBuffers(cmd, 0, 2, state.vertexBuffers, state.vertexOffsets);
                vkCmdDrawIndexedIndirectCount(cmd, state.indirectBuffer, 0,
                                              state.countBuffer, 0,
                                              state.instanceCount, state.drawStride);
            }
            vkCmdEndRendering(cmd);
        }
        m_ZPrepassState = nullptr;

        if (m_PbrPassState) {
            MNR_GPU_ZONE(m_GpuProfileCtx, cmd, "PBR Pass");
            const auto &state = *m_PbrPassState;
            VkRenderingAttachmentInfo colorAtt{};
            colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAtt.clearValue.color = {{0.05f, 0.05f, 0.07f, 1.f}};
            colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (state.msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
                colorAtt.imageView = state.msaaColorView;
                colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAtt.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                colorAtt.resolveImageView = state.offscreenColorView;
                colorAtt.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            } else {
                colorAtt.imageView = state.offscreenColorView;
                colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }

            VkRenderingAttachmentInfo depthAtt{};
            depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAtt.imageView = state.depthView;
            depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo ri{};
            ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            ri.renderArea.extent = state.extent;
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &colorAtt;
            ri.pDepthAttachment = &depthAtt;
            vkCmdBeginRendering(cmd, &ri);

            if (state.pipeline && state.indexBuffer && state.indirectBuffer && state.countBuffer) {
                VkViewport vp{
                    0.f, 0.f, static_cast<float>(state.extent.width), static_cast<float>(state.extent.height),
                    0.f, 1.f
                };
                VkRect2D sc{{0, 0}, state.extent};
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
                if (state.pipelineLayout && state.descriptorSetCount > 0) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            state.pipelineLayout, 0, state.descriptorSetCount,
                                            state.descriptorSets, 0, nullptr);
                }
                vkCmdBindIndexBuffer(cmd, state.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindVertexBuffers(cmd, 0, 2, state.vertexBuffers, state.vertexOffsets);
                vkCmdDrawIndexedIndirectCount(cmd, state.indirectBuffer, 0,
                                              state.countBuffer, 0,
                                              state.instanceCount, state.drawStride);
            }
            vkCmdEndRendering(cmd);
        }
        m_PbrPassState = nullptr;

        if (m_SkyboxPassState) {
            const auto &state = *m_SkyboxPassState;
            if (state.pipeline && state.vertexBuffer && state.indexBuffer && state.offscreenColorView) {
                MNR_GPU_ZONE(m_GpuProfileCtx, cmd, "Skybox Pass");
                VkRenderingAttachmentInfo colorAtt{};
                colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                if (state.msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
                    colorAtt.imageView = state.msaaColorView;
                    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    colorAtt.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                    colorAtt.resolveImageView = state.offscreenColorView;
                    colorAtt.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                } else {
                    colorAtt.imageView = state.offscreenColorView;
                    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                }
                colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingAttachmentInfo depthAtt{};
                depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAtt.imageView = state.depthView;
                depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingInfo ri{};
                ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                ri.renderArea.extent = state.extent;
                ri.layerCount = 1;
                ri.colorAttachmentCount = 1;
                ri.pColorAttachments = &colorAtt;
                ri.pDepthAttachment = (state.depthView != VK_NULL_HANDLE) ? &depthAtt : nullptr;
                vkCmdBeginRendering(cmd, &ri);

                VkViewport vp{
                    0.f, 0.f, static_cast<float>(state.extent.width), static_cast<float>(state.extent.height),
                    0.f, 1.f
                };
                VkRect2D sc{{0, 0}, state.extent};
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
                if (state.pipelineLayout && state.descriptorSet != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            state.pipelineLayout, 0, 1, &state.descriptorSet, 0, nullptr);
                }
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &state.vertexBuffer, &offset);
                vkCmdBindIndexBuffer(cmd, state.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, state.indexCount, 1, 0, 0, 0);
                vkCmdEndRendering(cmd);
            }
        }
        m_SkyboxPassState = nullptr;

        if (m_CompositePassState) {
            const auto &state = *m_CompositePassState;
            if (state.colorView && state.pipeline) {
                MNR_GPU_ZONE(m_GpuProfileCtx, cmd, "Composite Pass");
                VkRenderingAttachmentInfo colorAtt{};
                colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                colorAtt.imageView = state.colorView;
                colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingInfo ri{};
                ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                ri.renderArea.extent = state.extent;
                ri.layerCount = 1;
                ri.colorAttachmentCount = 1;
                ri.pColorAttachments = &colorAtt;
                vkCmdBeginRendering(cmd, &ri);

                VkViewport vp{
                    0.f, 0.f, static_cast<float>(state.extent.width), static_cast<float>(state.extent.height),
                    0.f, 1.f
                };
                VkRect2D sc{{0, 0}, state.extent};
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
                if (state.pipelineLayout && state.descriptorSet != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            state.pipelineLayout, 0, 1, &state.descriptorSet, 0, nullptr);
                    if (state.pushConstants && state.pushConstantSize > 0) {
                        vkCmdPushConstants(cmd, state.pipelineLayout,
                                           state.pushConstantStages, 0, state.pushConstantSize,
                                           state.pushConstants);
                    }
                }
                vkCmdDraw(cmd, 3, 1, 0, 0);
                vkCmdEndRendering(cmd);
            }
        }
        m_CompositePassState = nullptr;
    }
} // namespace Manro
