#include <Manro/Render/RHI/VulkanCommandList.h>
#include <volk.h>
#include <algorithm>

namespace Manro::RHI {

    void VulkanCommandList::ImportBuffer(BufferHandle handle, VkBuffer buffer) {
        m_Buffers[handle.packed] = buffer;
    }

    void VulkanCommandList::ImportTexture(TextureHandle handle, VulkanTextureBinding texture) {
        m_Textures[handle.packed] = texture;
    }

    void
    VulkanCommandList::ImportGraphicsPipeline(PipelineHandle handle, VkPipeline pipeline, VkPipelineLayout layout) {
        m_Pipelines[handle.packed] = VulkanPipelineBinding{pipeline, layout, VK_PIPELINE_BIND_POINT_GRAPHICS};
    }

    void VulkanCommandList::ExecuteZPrepass(const VulkanZPrepassState &state) {
        if (!m_CommandBuffer || !state.depthView || !state.pipeline || !state.indexBuffer || !state.indirectBuffer ||
            !state.countBuffer)
            return;

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
        vkCmdBeginRendering(m_CommandBuffer, &ri);

        VkViewport vp{0.f, 0.f, static_cast<float>(state.extent.width), static_cast<float>(state.extent.height), 0.f,
                      1.f};
        vkCmdSetViewport(m_CommandBuffer, 0, 1, &vp);
        VkRect2D sc{{0, 0}, state.extent};
        vkCmdSetScissor(m_CommandBuffer, 0, 1, &sc);

        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
        if (state.pipelineLayout && state.descriptorSetCount > 0) {
            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    state.pipelineLayout, 0, state.descriptorSetCount,
                                    state.descriptorSets, 0, nullptr);
        }

        vkCmdBindIndexBuffer(m_CommandBuffer, state.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindVertexBuffers(m_CommandBuffer, 0, 2, state.vertexBuffers, state.vertexOffsets);
        vkCmdDrawIndexedIndirectCount(m_CommandBuffer,
                                      state.indirectBuffer, 0,
                                      state.countBuffer, 0,
                                      state.instanceCount, state.drawStride);

        vkCmdEndRendering(m_CommandBuffer);
    }

    void VulkanCommandList::ExecutePbrPass(const VulkanPbrPassState &state) {
        if (!m_CommandBuffer || !state.pipeline || !state.indexBuffer || !state.indirectBuffer || !state.countBuffer)
            return;

        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.clearValue = state.clearColor;

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
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent = state.extent;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;
        ri.pDepthAttachment = &depthAtt;
        vkCmdBeginRendering(m_CommandBuffer, &ri);

        VkViewport vp{0.f, 0.f, static_cast<float>(state.extent.width), static_cast<float>(state.extent.height), 0.f,
                      1.f};
        VkRect2D sc{{0, 0}, state.extent};
        vkCmdSetViewport(m_CommandBuffer, 0, 1, &vp);
        vkCmdSetScissor(m_CommandBuffer, 0, 1, &sc);

        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
        if (state.pipelineLayout && state.descriptorSetCount > 0) {
            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    state.pipelineLayout, 0, state.descriptorSetCount,
                                    state.descriptorSets, 0, nullptr);
        }

        vkCmdBindIndexBuffer(m_CommandBuffer, state.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindVertexBuffers(m_CommandBuffer, 0, 2, state.vertexBuffers, state.vertexOffsets);
        vkCmdDrawIndexedIndirectCount(m_CommandBuffer,
                                      state.indirectBuffer, 0,
                                      state.countBuffer, 0,
                                      state.instanceCount, state.drawStride);
        vkCmdEndRendering(m_CommandBuffer);
    }

    void VulkanCommandList::ExecuteCompositePass(const VulkanCompositePassState &state) {
        if (!m_CommandBuffer || !state.colorView || !state.pipeline) return;

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
        vkCmdBeginRendering(m_CommandBuffer, &ri);

        VkViewport vp{0.f, 0.f, static_cast<float>(state.extent.width), static_cast<float>(state.extent.height), 0.f,
                      1.f};
        vkCmdSetViewport(m_CommandBuffer, 0, 1, &vp);
        VkRect2D sc{{0, 0}, state.extent};
        vkCmdSetScissor(m_CommandBuffer, 0, 1, &sc);

        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
        if (state.pipelineLayout && state.descriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    state.pipelineLayout, 0, 1, &state.descriptorSet, 0, nullptr);
            if (state.pushConstants && state.pushConstantSize > 0) {
                vkCmdPushConstants(m_CommandBuffer, state.pipelineLayout,
                                   state.pushConstantStages, 0, state.pushConstantSize,
                                   state.pushConstants);
            }
        }
        vkCmdDraw(m_CommandBuffer, 3, 1, 0, 0);
        vkCmdEndRendering(m_CommandBuffer);
    }

    void VulkanCommandList::BeginRendering(std::span<const ColorAttachment> color, const DepthAttachment *depth) {
        if (!m_CommandBuffer) return;

        VkRenderingAttachmentInfo colorInfos[8]{};
        const u32 colorCount = static_cast<u32>(std::min<size_t>(color.size(), 8u));

        for (u32 i = 0; i < colorCount; ++i) {
            auto it = m_Textures.find(color[i].texture.packed);
            if (it == m_Textures.end() || !it->second.view) return;

            colorInfos[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorInfos[i].imageView = it->second.view;
            colorInfos[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorInfos[i].loadOp = static_cast<VkAttachmentLoadOp>(color[i].loadOp);
            colorInfos[i].storeOp = static_cast<VkAttachmentStoreOp>(color[i].storeOp);
            colorInfos[i].clearValue.color.float32[0] = color[i].clear[0];
            colorInfos[i].clearValue.color.float32[1] = color[i].clear[1];
            colorInfos[i].clearValue.color.float32[2] = color[i].clear[2];
            colorInfos[i].clearValue.color.float32[3] = color[i].clear[3];
        }

        VkRenderingAttachmentInfo depthInfo{};
        VkRenderingAttachmentInfo *depthPtr = nullptr;
        if (depth) {
            auto it = m_Textures.find(depth->texture.packed);
            if (it == m_Textures.end() || !it->second.view) return;
            depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthInfo.imageView = it->second.view;
            depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthInfo.loadOp = static_cast<VkAttachmentLoadOp>(depth->loadOp);
            depthInfo.storeOp = static_cast<VkAttachmentStoreOp>(depth->storeOp);
            depthInfo.clearValue.depthStencil = {depth->clearDepth, 0};
            depthPtr = &depthInfo;
        }

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea = {{0, 0},
                         {1, 1}};
        ri.layerCount = 1;
        ri.colorAttachmentCount = colorCount;
        ri.pColorAttachments = colorInfos;
        ri.pDepthAttachment = depthPtr;
        vkCmdBeginRendering(m_CommandBuffer, &ri);
    }

    void VulkanCommandList::EndRendering() {
        if (m_CommandBuffer) vkCmdEndRendering(m_CommandBuffer);
    }

    void VulkanCommandList::SetViewport(const Viewport &vp) {
        if (!m_CommandBuffer) return;
        VkViewport v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
        vkCmdSetViewport(m_CommandBuffer, 0, 1, &v);
    }

    void VulkanCommandList::SetScissor(const Scissor &sc) {
        if (!m_CommandBuffer) return;
        VkRect2D r{{sc.x,     sc.y},
                   {sc.width, sc.height}};
        vkCmdSetScissor(m_CommandBuffer, 0, 1, &r);
    }

    void VulkanCommandList::BindPipeline(PipelineHandle pso) {
        if (!m_CommandBuffer) return;
        auto it = m_Pipelines.find(pso.packed);
        if (it == m_Pipelines.end() || !it->second.pipeline) return;
        m_CurrentLayout = it->second.layout;
        m_CurrentBindPoint = it->second.bindPoint;
        vkCmdBindPipeline(m_CommandBuffer, it->second.bindPoint, it->second.pipeline);
    }

    void VulkanCommandList::BindVertexBuffer(u32 slot, BufferHandle buf, u64 offset) {
        if (!m_CommandBuffer) return;
        auto it = m_Buffers.find(buf.packed);
        if (it == m_Buffers.end() || !it->second) return;
        VkDeviceSize off = offset;
        vkCmdBindVertexBuffers(m_CommandBuffer, slot, 1, &it->second, &off);
    }

    void VulkanCommandList::BindIndexBuffer(BufferHandle buf, u64 offset) {
        if (!m_CommandBuffer) return;
        auto it = m_Buffers.find(buf.packed);
        if (it == m_Buffers.end() || !it->second) return;
        vkCmdBindIndexBuffer(m_CommandBuffer, it->second, offset, VK_INDEX_TYPE_UINT32);
    }

    void VulkanCommandList::PushConstants(const void *data, u32 size, u32 offset) {
        if (!m_CommandBuffer || !m_CurrentLayout || !data || size == 0) return;
        vkCmdPushConstants(m_CommandBuffer, m_CurrentLayout,
                           VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT,
                           offset, size, data);
    }

    void VulkanCommandList::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset,
                                        u32 firstInstance) {
        if (!m_CommandBuffer) return;
        vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void VulkanCommandList::DrawIndexedIndirectCount(BufferHandle indirectBuf, u64 indirectOffset,
                                                     BufferHandle countBuf, u64 countOffset,
                                                     u32 maxDrawCount, u32 stride) {
        if (!m_CommandBuffer) return;
        auto indirectIt = m_Buffers.find(indirectBuf.packed);
        auto countIt = m_Buffers.find(countBuf.packed);
        if (indirectIt == m_Buffers.end() || countIt == m_Buffers.end()) return;
        vkCmdDrawIndexedIndirectCount(m_CommandBuffer,
                                      indirectIt->second, indirectOffset,
                                      countIt->second, countOffset,
                                      maxDrawCount, stride);
    }

    void VulkanCommandList::Dispatch(u32 x, u32 y, u32 z) {
        if (m_CommandBuffer) vkCmdDispatch(m_CommandBuffer, x, y, z);
    }

    void VulkanCommandList::TextureBarrier(TextureHandle tex,
                                           u32 srcStageMask, u32 srcAccessMask,
                                           u32 dstStageMask, u32 dstAccessMask,
                                           u32 oldLayout, u32 newLayout) {
        if (!m_CommandBuffer) return;
        auto it = m_Textures.find(tex.packed);
        if (it == m_Textures.end() || !it->second.image) return;

        VkImageMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = srcStageMask;
        b.srcAccessMask = srcAccessMask;
        b.dstStageMask = dstStageMask;
        b.dstAccessMask = dstAccessMask;
        b.oldLayout = static_cast<VkImageLayout>(oldLayout);
        b.newLayout = static_cast<VkImageLayout>(newLayout);
        b.image = it->second.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(m_CommandBuffer, &dep);
    }

    void VulkanCommandList::BufferBarrier(BufferHandle buf,
                                          u32 srcStageMask, u32 srcAccessMask,
                                          u32 dstStageMask, u32 dstAccessMask) {
        if (!m_CommandBuffer) return;
        auto it = m_Buffers.find(buf.packed);
        if (it == m_Buffers.end() || !it->second) return;

        VkBufferMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        b.srcStageMask = srcStageMask;
        b.srcAccessMask = srcAccessMask;
        b.dstStageMask = dstStageMask;
        b.dstAccessMask = dstAccessMask;
        b.buffer = it->second;
        b.offset = 0;
        b.size = VK_WHOLE_SIZE;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &b;
        vkCmdPipelineBarrier2(m_CommandBuffer, &dep);
    }

    void VulkanCommandList::FillBuffer(BufferHandle buf, u64 offset, u64 size, u32 value) {
        if (!m_CommandBuffer) return;
        auto it = m_Buffers.find(buf.packed);
        if (it == m_Buffers.end() || !it->second) return;
        vkCmdFillBuffer(m_CommandBuffer, it->second, offset, size, value);
    }

    void VulkanCommandList::BeginDebugLabel(const char *name) {
        if (!m_CommandBuffer || !vkCmdBeginDebugUtilsLabelEXT) return;
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        vkCmdBeginDebugUtilsLabelEXT(m_CommandBuffer, &label);
    }

    void VulkanCommandList::EndDebugLabel() {
        if (m_CommandBuffer && vkCmdEndDebugUtilsLabelEXT)
            vkCmdEndDebugUtilsLabelEXT(m_CommandBuffer);
    }

    void VulkanCommandList::SetDepthBias(float constant, float clamp, float slope) {
        if (!m_CommandBuffer) return;
        vkCmdSetDepthBias(m_CommandBuffer, constant, clamp, slope);
    }

    void VulkanCommandList::BindDescriptorSets(u32 firstSet, const void **sets, u32 setCount) {
        if (!m_CommandBuffer || !m_CurrentLayout || !sets || setCount == 0) return;
        vkCmdBindDescriptorSets(m_CommandBuffer, m_CurrentBindPoint, m_CurrentLayout,
                                firstSet, setCount,
                                reinterpret_cast<const VkDescriptorSet *>(const_cast<void **>(sets)),
                                0, nullptr);
    }

} // namespace Manro::RHI

