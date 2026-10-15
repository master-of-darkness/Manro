#include "VoxelEncoder.h"
#include "Buffer.h"
#include "SparseBuffer.h"
#include "Pipeline.h"
#include "TileArray.h"
#include "../Voxel/VoxelTypes.h"

#include <vector>

namespace Manro {
    CVoxelEncoder::CVoxelEncoder(VkCommandBuffer cmd, VkImageView color, VkImageView depth, u32 width,
                                 u32 height, bool clearColor)
        : m_Cmd(cmd), m_Color(color), m_Depth(depth), m_Width(width), m_Height(height),
          m_Clear(clearColor) {
    }

    void CVoxelEncoder::FillBuffer(const CBuffer &buf, u64 offset, u64 size) {
        vkCmdFillBuffer(m_Cmd, buf.GetHandle(), static_cast<VkDeviceSize>(offset),
                        static_cast<VkDeviceSize>(size), 0);
    }

    void CVoxelEncoder::CopyStaging(const CBuffer &staging, CSparseBuffer &bricks,
                                    const BufferCopyRegion *regions, u32 count) {
        if (count == 0 || !regions) return;
        std::vector<VkBufferCopy> copies(count);
        for (u32 i = 0; i < count; ++i) {
            copies[i].srcOffset = static_cast<VkDeviceSize>(regions[i].srcOffset);
            copies[i].dstOffset = static_cast<VkDeviceSize>(regions[i].dstOffset);
            copies[i].size = static_cast<VkDeviceSize>(regions[i].size);
        }
        vkCmdCopyBuffer(m_Cmd, staging.GetHandle(), bricks.NativeBuffer(), count, copies.data());
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                               VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.buffer = bricks.NativeBuffer();
        barrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(m_Cmd, &dep);
    }

    void CVoxelEncoder::BarrierEditUpload(const CBuffer &editStaging) {
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.buffer = editStaging.GetHandle();
        barrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(m_Cmd, &dep);
    }

    void CVoxelEncoder::DispatchEdit(const CPipeline &pipe, const CBuffer &editStaging,
                                     const VoxelEditDispatch &desc) {
        BarrierEditUpload(editStaging);
        VoxelEditPushConstants_t pc{};
        pc.brickBufferAddr = desc.brickBufferAddr;
        pc.headerAddr = desc.headerAddr;
        pc.pageTableAddr = desc.pageTableAddr;
        pc.editAddr = desc.editAddr;
        pc.worldMin = desc.worldMin;
        pc.brickSize = desc.brickSize;
        pc.editCount = desc.editCount;
        pc.virtualDim = desc.virtualDim;
        vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.GetHandle());
        vkCmdPushConstants(m_Cmd, pipe.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(m_Cmd, pc.editCount, 1, 1);
        VkMemoryBarrier2 post{};
        post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        post.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        post.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        post.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                            VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
        post.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        VkDependencyInfo postDep{};
        postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        postDep.memoryBarrierCount = 1;
        postDep.pMemoryBarriers = &post;
        vkCmdPipelineBarrier2(m_Cmd, &postDep);
    }

    void CVoxelEncoder::DispatchGi(const CPipeline &pipe, const VoxelGiDispatch &desc, u32 groups) {
        VoxelGiPushConstants_t pc{};
        pc.brickBufferAddr = desc.brickBufferAddr;
        pc.headerAddr = desc.headerAddr;
        pc.cascadeAddr = desc.cascadeAddr;
        pc.cascadeDstAddr = desc.cascadeDstAddr;
        pc.sunAddr = desc.sunAddr;
        pc.brickCount = desc.brickCount;
        pc.cascadeRes = desc.cascadeRes;
        pc.cascadeCount = desc.cascadeCount;
        pc.brickSize = desc.brickSize;
        pc.worldMin = desc.worldMin;
        vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.GetHandle());
        vkCmdPushConstants(m_Cmd, pipe.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(m_Cmd, groups, groups, groups);
    }

    void CVoxelEncoder::BarrierGiToGi() {
        VkMemoryBarrier2 mem{};
        mem.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mem.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        mem.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mem;
        vkCmdPipelineBarrier2(m_Cmd, &dep);
    }

    void CVoxelEncoder::BarrierGiToFrag() {
        VkMemoryBarrier2 mem{};
        mem.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mem.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        mem.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        mem.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mem;
        vkCmdPipelineBarrier2(m_Cmd, &dep);
    }

    void CVoxelEncoder::BarrierFrame(const VoxelFrameBarriers &bar) {
        VkBufferMemoryBarrier2 b[7]{};
        u32 barrierCount = 0;
        auto pushBuf = [&](VkBuffer buf, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                            VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, u64 offset,
                            u64 size) {
            VkBufferMemoryBarrier2 &e = b[barrierCount++];
            e.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            e.srcStageMask = srcStage;
            e.srcAccessMask = srcAccess;
            e.dstStageMask = dstStage;
            e.dstAccessMask = dstAccess;
            e.buffer = buf;
            e.offset = static_cast<VkDeviceSize>(offset);
            e.size = static_cast<VkDeviceSize>(size);
        };
        if (bar.params)
            pushBuf(bar.params->GetHandle(), VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
                    VK_ACCESS_2_SHADER_READ_BIT, 0, bar.paramsSize);
        if (bar.vis)
            pushBuf(bar.vis->GetHandle(), VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_READ_BIT, 0,
                    bar.visSize);
        if (bar.frag)
            pushBuf(bar.frag->GetHandle(), VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, 0,
                    bar.fragSize);
        if (bar.headerOrNull)
            pushBuf(bar.headerOrNull->GetHandle(),
                    VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT,
                    VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_READ_BIT, 0,
                    VK_WHOLE_SIZE);
        if (bar.bricksOrNull)
            pushBuf(bar.bricksOrNull->NativeBuffer(), VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
                    VK_ACCESS_2_SHADER_READ_BIT, 0, VK_WHOLE_SIZE);
        if (bar.faceCacheOrNull)
            pushBuf(bar.faceCacheOrNull->GetHandle(), VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT,
                    VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
                    VK_ACCESS_2_SHADER_READ_BIT, 0, VK_WHOLE_SIZE);
        if (bar.debug && bar.debugOrNull)
            pushBuf(bar.debugOrNull->GetHandle(),
                    VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, 0, VK_WHOLE_SIZE);
        if (barrierCount == 0) return;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = barrierCount;
        dep.pBufferMemoryBarriers = b;
        vkCmdPipelineBarrier2(m_Cmd, &dep);
    }

    void CVoxelEncoder::BeginMeshPass() {
        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.imageView = m_Color;
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = m_Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.clearValue.color = {{0.05f, 0.05f, 0.07f, 1.f}};

        VkRenderingAttachmentInfo depthAtt{};
        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAtt.imageView = m_Depth;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = m_Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.clearValue.depthStencil = {1.f, 0};

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent = {m_Width, m_Height};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;
        ri.pDepthAttachment = &depthAtt;
        vkCmdBeginRendering(m_Cmd, &ri);

        VkViewport vp{0.f, 0.f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.f, 1.f};
        vkCmdSetViewport(m_Cmd, 0, 1, &vp);
        VkRect2D sc{{0, 0}, {m_Width, m_Height}};
        vkCmdSetScissor(m_Cmd, 0, 1, &sc);
    }

    void CVoxelEncoder::DrawVoxelMesh(const CPipeline &pipe, const CTileArray &tiles, const void *rootBytes,
                                      u64 rootSize, u32 groupCount) {
        vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.GetHandle());
        vkCmdPushConstants(m_Cmd, pipe.GetLayout(),
                           VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT |
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, static_cast<u32>(rootSize), rootBytes);
        VkDescriptorSet set = tiles.NativeSet();
        if (set != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(m_Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.GetLayout(), 0, 1,
                                    &set, 0, nullptr);
        }
        if (groupCount > 0)
            vkCmdDrawMeshTasksEXT(m_Cmd, groupCount, 1, 1);
    }

    void CVoxelEncoder::EndMeshPass() {
        vkCmdEndRendering(m_Cmd);
    }

    void CVoxelEncoder::BarrierMeshToHost() {
        VkMemoryBarrier2 mem{};
        mem.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mem.srcStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                           VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
        mem.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        mem.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        mem.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mem;
        vkCmdPipelineBarrier2(m_Cmd, &dep);
    }
}
