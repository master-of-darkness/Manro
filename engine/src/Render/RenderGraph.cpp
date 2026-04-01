#include <Manro/Render/RenderGraph.h>

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <unordered_map>

namespace Manro {
    void RGPassBuilder::WriteColor(RGTextureHandle h, VkAttachmentLoadOp loadOp,
                                   VkPipelineStageFlags2 stage) {
        RGTextureAccess acc{};
        acc.handle = h;
        acc.stageMask = stage;
        acc.accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        acc.requiredLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        acc.loadOp = loadOp;
        acc.isAttachment = true;
        acc.isDepth = false;
        m_TextureAccesses.push_back(acc);
    }

    void RGPassBuilder::WriteDepth(RGTextureHandle h, VkAttachmentLoadOp loadOp,
                                   VkPipelineStageFlags2 stage) {
        RGTextureAccess acc{};
        acc.handle = h;
        acc.stageMask = stage;
        acc.accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        acc.requiredLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        acc.loadOp = loadOp;
        acc.isAttachment = true;
        acc.isDepth = true;
        m_TextureAccesses.push_back(acc);
    }

    void RGPassBuilder::ReadTexture(RGTextureHandle h, VkPipelineStageFlags2 stage) {
        RGTextureAccess acc{};
        acc.handle = h;
        acc.stageMask = stage;
        acc.accessMask = VK_ACCESS_2_SHADER_READ_BIT;
        acc.requiredLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        acc.isAttachment = false;
        m_TextureAccesses.push_back(acc);
    }

    void RGPassBuilder::WriteStorageImage(RGTextureHandle h, VkPipelineStageFlags2 stage) {
        RGTextureAccess acc{};
        acc.handle = h;
        acc.stageMask = stage;
        acc.accessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        acc.requiredLayout = VK_IMAGE_LAYOUT_GENERAL;
        acc.isAttachment = false;
        m_TextureAccesses.push_back(acc);
    }

    void RGPassBuilder::ReadBuffer(RGBufferHandle h, VkPipelineStageFlags2 stage,
                                   VkAccessFlags2 access) {
        m_BufferAccesses.push_back({h, stage, access});
    }

    void RGPassBuilder::WriteBuffer(RGBufferHandle h, VkPipelineStageFlags2 stage,
                                    VkAccessFlags2 access) {
        m_BufferAccesses.push_back({h, stage, access});
    }

    RGTextureHandle RenderGraph::DeclareTexture(RGTextureDesc desc) {
        u32 idx = m_NextTextureIdx++;
        if (idx >= m_TextureDescs.size()) m_TextureDescs.resize(idx + 1);
        m_TextureDescs[idx] = std::move(desc);
        RGTextureHandle h;
        h.packed = Handle<RGTextureTag>::Make(idx, 0).packed;
        m_NeedsCompile = true;
        return h;
    }

    RGBufferHandle RenderGraph::DeclareBuffer(RGBufferDesc desc) {
        u32 idx = m_NextBufferIdx++;
        if (idx >= m_BufferDescs.size()) m_BufferDescs.resize(idx + 1);
        m_BufferDescs[idx] = std::move(desc);
        RGBufferHandle h;
        h.packed = Handle<RGBufferTag>::Make(idx, 0).packed;
        m_NeedsCompile = true;
        return h;
    }

    void RenderGraph::ImportTexture(RGTextureHandle handle, RGResolvedTexture resolved) {
        m_Resources.BindTexture(handle, resolved);
    }

    void RenderGraph::ImportBuffer(RGBufferHandle handle, RGResolvedBuffer resolved) {
        m_Resources.BindBuffer(handle, resolved);
    }

    void RenderGraph::AddPass(std::string name, QueueType queue, PassSetupFn setup) {
        RGPassBuilder builder;
        setup(builder);

        PassDesc pd;
        pd.name = std::move(name);
        pd.queue = queue;
        pd.textureAccesses = builder.TextureAccesses();
        pd.bufferAccesses = builder.BufferAccesses();
        pd.execute = builder.Execute();
        m_Passes.push_back(std::move(pd));
        m_NeedsCompile = true;
    }

    void RenderGraph::Compile() {
        m_Compiled.clear();
        m_Compiled.reserve(m_Passes.size());

        std::unordered_map<u32, TextureState> textureStates;
        std::unordered_map<u32, BufferState> bufferStates;

        for (const auto &pd: m_Passes) {
            CompiledPass cp;
            cp.name = pd.name;
            cp.queue = pd.queue;
            cp.execute = pd.execute;

            for (const auto &acc: pd.textureAccesses) {
                u32 key = acc.handle.packed;

                TextureState src = textureStates[key]; // zero = initial undefined state
                TextureState dst;
                dst.stage = acc.stageMask;
                dst.access = acc.accessMask;
                dst.layout = acc.requiredLayout;

                bool needsBarrier = (src.layout != dst.layout)
                                    || (src.access != dst.access);

                if (needsBarrier) {
                    VkImageMemoryBarrier2 b{};
                    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    b.srcStageMask = src.stage;
                    b.srcAccessMask = src.access;
                    b.dstStageMask = dst.stage;
                    b.dstAccessMask = dst.access;
                    b.oldLayout = src.layout;
                    b.newLayout = dst.layout;
                    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.image = VK_NULL_HANDLE; // sentinel; filled in Execute
                    b.subresourceRange = {
                            acc.isDepth
                            ? (VkImageAspectFlags) VK_IMAGE_ASPECT_DEPTH_BIT
                            : (VkImageAspectFlags) VK_IMAGE_ASPECT_COLOR_BIT,
                            0, 1, 0, 1
                    };

                    cp.imageBarriers.push_back(b);
                }

                textureStates[key] = dst;

                if (acc.isAttachment) {
                    if (acc.isDepth) cp.depthAttachments.push_back(acc);
                    else cp.colorAttachments.push_back(acc);
                }
            }

            for (const auto &acc: pd.bufferAccesses) {
                u32 key = acc.handle.packed;
                BufferState src = bufferStates[key];
                BufferState dst{acc.stageMask, acc.accessMask};

                bool needsBarrier = (src.access != 0) &&
                                    ((src.access != dst.access) ||
                                     (src.stage != dst.stage));
                if (needsBarrier) {
                    VkBufferMemoryBarrier2 b{};
                    b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    b.srcStageMask = src.stage;
                    b.srcAccessMask = src.access;
                    b.dstStageMask = dst.stage;
                    b.dstAccessMask = dst.access;
                    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.buffer = VK_NULL_HANDLE; // filled at Execute
                    b.offset = 0;
                    b.size = VK_WHOLE_SIZE;
                    cp.bufferBarriers.push_back(b);
                }
                bufferStates[key] = dst;
            }

            m_Compiled.push_back(std::move(cp));
        }

        m_NeedsCompile = false;
        LOG_INFO("[RenderGraph] Compiled {} passes", m_Compiled.size());
    }

    void RenderGraph::Execute(VkCommandBuffer cmd, VkExtent2D swapchainExtent) {
        if (m_NeedsCompile) Compile();

        m_SwapchainExtent = swapchainExtent;

        for (u32 pi = 0; pi < (u32) m_Compiled.size(); ++pi) {
            const CompiledPass &cp = m_Compiled[pi];
            const PassDesc &pd = m_Passes[pi];

#if !defined(NDEBUG)
            VkDebugUtilsLabelEXT label{};
            label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pLabelName = cp.name.c_str();
            if (vkCmdBeginDebugUtilsLabelEXT)
                vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
#endif
            std::vector<VkImageMemoryBarrier2> imageBarriers;
            std::vector<VkBufferMemoryBarrier2> bufferBarriers;

            for (const auto &acc: pd.textureAccesses) {
                try {
                    const auto &resolved = m_Resources.GetTexture(acc.handle);
                    if (resolved.image == VK_NULL_HANDLE) continue;

                    VkImageMemoryBarrier2 b{};
                    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                    b.dstStageMask = acc.stageMask;
                    b.dstAccessMask = acc.accessMask;
                    b.oldLayout = resolved.layout;
                    b.newLayout = acc.requiredLayout;
                    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.image = resolved.image;
                    b.subresourceRange = {
                            acc.isDepth
                            ? (VkImageAspectFlags) VK_IMAGE_ASPECT_DEPTH_BIT
                            : (VkImageAspectFlags) VK_IMAGE_ASPECT_COLOR_BIT,
                            0, 1, 0, 1
                    };

                    if (resolved.layout != acc.requiredLayout)
                        imageBarriers.push_back(b);

                    RGResolvedTexture updated = resolved;
                    updated.layout = acc.requiredLayout;
                    m_Resources.BindTexture(acc.handle, updated);
                } catch (...) {
                    LOG_WARN("[RenderGraph] Pass '{}': texture not imported for handle {}",
                             cp.name, acc.handle.packed);
                }
            }

            for (const auto &acc: pd.bufferAccesses) {
                try {
                    const auto &resolved = m_Resources.GetBuffer(acc.handle);
                    if (resolved.buffer == VK_NULL_HANDLE) continue;

                    VkBufferMemoryBarrier2 b{};
                    b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                    b.dstStageMask = acc.stageMask;
                    b.dstAccessMask = acc.accessMask;
                    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.buffer = resolved.buffer;
                    b.offset = 0;
                    b.size = VK_WHOLE_SIZE;
                    bufferBarriers.push_back(b);
                } catch (...) {
                    LOG_WARN("[RenderGraph] Pass '{}': buffer not imported for handle {}",
                             cp.name, acc.handle.packed);
                }
            }

            if (!imageBarriers.empty() || !bufferBarriers.empty()) {
                VkDependencyInfo dep{};
                dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep.imageMemoryBarrierCount = (u32) imageBarriers.size();
                dep.pImageMemoryBarriers = imageBarriers.data();
                dep.bufferMemoryBarrierCount = (u32) bufferBarriers.size();
                dep.pBufferMemoryBarriers = bufferBarriers.data();
                vkCmdPipelineBarrier2(cmd, &dep);
            }

            bool hasRendering = !cp.colorAttachments.empty() || !cp.depthAttachments.empty();
            if (hasRendering) {
                std::vector<VkRenderingAttachmentInfo> colorAtts;
                colorAtts.reserve(cp.colorAttachments.size());

                for (const auto &acc: cp.colorAttachments) {
                    try {
                        const auto &res = m_Resources.GetTexture(acc.handle);
                        VkRenderingAttachmentInfo att{};
                        att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                        att.imageView = res.view;
                        att.imageLayout = acc.requiredLayout;
                        att.loadOp = acc.loadOp;
                        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                        att.clearValue.color = {{0.f, 0.f, 0.f, 1.f}};
                        colorAtts.push_back(att);
                    } catch (...) {
                    }
                }

                VkRenderingAttachmentInfo depthAtt{};
                bool hasDepth = !cp.depthAttachments.empty();
                if (hasDepth) {
                    try {
                        const auto &acc = cp.depthAttachments[0];
                        const auto &res = m_Resources.GetTexture(acc.handle);
                        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                        depthAtt.imageView = res.view;
                        depthAtt.imageLayout = acc.requiredLayout;
                        depthAtt.loadOp = acc.loadOp;
                        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                        depthAtt.clearValue.depthStencil = {1.f, 0};
                    } catch (...) { hasDepth = false; }
                }

                VkRenderingInfo ri{};
                ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                ri.renderArea.extent = swapchainExtent;
                ri.layerCount = 1;
                ri.colorAttachmentCount = (u32) colorAtts.size();
                ri.pColorAttachments = colorAtts.data();
                ri.pDepthAttachment = hasDepth ? &depthAtt : nullptr;

                vkCmdBeginRendering(cmd, &ri);
            }

            if (cp.execute) cp.execute(cmd, m_Resources);

            if (hasRendering) vkCmdEndRendering(cmd);

#if !defined(NDEBUG)
            if (vkCmdEndDebugUtilsLabelEXT) vkCmdEndDebugUtilsLabelEXT(cmd);
#endif
        }
    }

    void RenderGraph::ResetImports() {
        m_Resources = RGResources{};
    }

    void RenderGraph::Reset() {
        m_Passes.clear();
        m_Compiled.clear();
        m_NeedsCompile = true;
        ResetImports();
    }
} // namespace Manro
