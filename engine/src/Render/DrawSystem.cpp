#include "Internal/DrawSystem.h"
#include "Vulkan/Buffer.h"
#include "Vulkan/Pipeline.h"
#include "Vulkan/VulkanContext.h"

#include <Manro/Core/VirtualFS.h>
#include <Manro/Core/Logger.h>
#include <volk.h>

namespace Manro {
    DrawSystem::DrawSystem(const VulkanContext &context)
        : m_Context(context) {
    }

    DrawSystem::~DrawSystem() {
        Shutdown();
    }

    void DrawSystem::Init(VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples) {
        CreateBuffers();
        CreateComputePipeline();
        CreateRenderPipelines(colorFormat, depthFormat, msaaSamples);
    }

    void DrawSystem::Shutdown() {
        if (m_DescriptorPool) {
            vkDestroyDescriptorPool(m_Context.GetDevice(), m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        if (m_ComputeSetLayout) {
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_ComputeSetLayout, nullptr);
            m_ComputeSetLayout = VK_NULL_HANDLE;
        }

        m_ExpandPipeline.reset();
        m_RenderPipeline.reset();
        m_RenderPipelineNoDepth.reset();

        m_LineCommandBuffer.reset();
        m_BoxCommandBuffer.reset();
        m_SphereCommandBuffer.reset();
        m_FrustumCommandBuffer.reset();
        m_CrossCommandBuffer.reset();
        m_VertexBuffer.reset();
        m_VertexBufferNoDepth.reset();
        m_IndirectBuffer.reset();
        m_IndirectBufferNoDepth.reset();
        m_CounterBuffer.reset();
    }

    void DrawSystem::CreateBuffers() {
        m_LineCommandBuffer = CreateScope<Buffer>(
            m_Context,
            sizeof(DrawLineCmd) * kMaxLines,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        m_BoxCommandBuffer = CreateScope<Buffer>(
            m_Context,
            sizeof(DrawBoxCmd) * kMaxBoxes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        m_SphereCommandBuffer = CreateScope<Buffer>(
            m_Context,
            sizeof(DrawSphereCmd) * kMaxSpheres,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        m_FrustumCommandBuffer = CreateScope<Buffer>(
            m_Context,
            sizeof(DrawFrustumCmd) * kMaxFrustums,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        m_CrossCommandBuffer = CreateScope<Buffer>(
            m_Context,
            sizeof(DrawCrossCmd) * kMaxCrosses,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        m_VertexBuffer = CreateScope<Buffer>(
            m_Context,
            sizeof(LineVertex) * kMaxVertices,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        m_VertexBufferNoDepth = CreateScope<Buffer>(
            m_Context,
            sizeof(LineVertex) * kMaxVertices,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        m_IndirectBuffer = CreateScope<Buffer>(
            m_Context,
            sizeof(DrawIndirectCmd),
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        m_IndirectBufferNoDepth = CreateScope<Buffer>(
            m_Context,
            sizeof(DrawIndirectCmd),
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        m_CounterBuffer = CreateScope<Buffer>(
            m_Context,
            sizeof(u32) * 2,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );
    }

    void DrawSystem::CreateComputePipeline() {
        VkDevice device = m_Context.GetDevice();

        // Create descriptor set layout
        VkDescriptorSetLayoutBinding bindings[10] = {};
        for (int i = 0; i < 10; i++) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 10;
        layoutInfo.pBindings = bindings;

        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_ComputeSetLayout);

        // Create descriptor pool
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 20;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool);

        // Allocate descriptor sets
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_ComputeSetLayout;

        vkAllocateDescriptorSets(device, &allocInfo, &m_ComputeDescriptorSet);
        vkAllocateDescriptorSets(device, &allocInfo, &m_ComputeDescriptorSetNoDepth);

        // Update descriptor sets
        VkDescriptorBufferInfo bufferInfos[10] = {};
        bufferInfos[0].buffer = m_LineCommandBuffer->GetHandle();
        bufferInfos[0].range = VK_WHOLE_SIZE;
        bufferInfos[1].buffer = m_BoxCommandBuffer->GetHandle();
        bufferInfos[1].range = VK_WHOLE_SIZE;
        bufferInfos[2].buffer = m_SphereCommandBuffer->GetHandle();
        bufferInfos[2].range = VK_WHOLE_SIZE;
        bufferInfos[3].buffer = m_FrustumCommandBuffer->GetHandle();
        bufferInfos[3].range = VK_WHOLE_SIZE;
        bufferInfos[4].buffer = m_CrossCommandBuffer->GetHandle();
        bufferInfos[4].range = VK_WHOLE_SIZE;
        bufferInfos[5].buffer = m_VertexBuffer->GetHandle();
        bufferInfos[5].range = VK_WHOLE_SIZE;
        bufferInfos[6].buffer = m_VertexBufferNoDepth->GetHandle();
        bufferInfos[6].range = VK_WHOLE_SIZE;
        bufferInfos[7].buffer = m_IndirectBuffer->GetHandle();
        bufferInfos[7].range = VK_WHOLE_SIZE;
        bufferInfos[8].buffer = m_IndirectBufferNoDepth->GetHandle();
        bufferInfos[8].range = VK_WHOLE_SIZE;
        bufferInfos[9].buffer = m_CounterBuffer->GetHandle();
        bufferInfos[9].range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[10] = {};
        for (int i = 0; i < 10; i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_ComputeDescriptorSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &bufferInfos[i];
        }

        vkUpdateDescriptorSets(device, 10, writes, 0, nullptr);

        // Load compute shader
        auto compSpv = VirtualFS::Get().ReadFile("shaders://line_expand.comp.spv");
        if (compSpv.empty()) {
            LOG_ERROR("[DrawSystem] Failed to load line_expand compute shader");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.descriptorSetLayouts = {m_ComputeSetLayout};
        cfg.pushConstantSize = sizeof(u32) * 5;
        cfg.pushConstantStages = VK_SHADER_STAGE_COMPUTE_BIT;

        m_ExpandPipeline = CreateScope<Pipeline>(m_Context);
        m_ExpandPipeline->BuildCompute(compSpv, cfg);
    }

    void DrawSystem::CreateRenderPipelines(VkFormat colorFormat, VkFormat depthFormat,
                                           VkSampleCountFlagBits msaaSamples) {
        auto vertSpv = VirtualFS::Get().ReadFile("shaders://gizmo.vert.spv");
        auto fragSpv = VirtualFS::Get().ReadFile("shaders://gizmo.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[DrawSystem] Gizmo shaders not found");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        cfg.colorAttachmentFormat = colorFormat;
        cfg.depthAttachmentFormat = depthFormat;
        cfg.msaaSamples = msaaSamples;
        cfg.vertexInputBindings = {
            {0, sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX}
        };
        cfg.vertexInputAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineVertex, position)},
            {1, 0, VK_FORMAT_R32_UINT, offsetof(LineVertex, color)},
        };
        cfg.pushConstantSize = sizeof(Mat4);
        cfg.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
        cfg.depthWriteEnable = VK_FALSE;
        cfg.depthCompareOp = VK_COMPARE_OP_LESS;
        cfg.depthTestEnable = VK_TRUE;

        m_RenderPipeline = CreateScope<Pipeline>(m_Context);
        m_RenderPipeline->BuildGraphics(vertSpv, fragSpv, cfg);

        cfg.depthTestEnable = VK_FALSE;
        m_RenderPipelineNoDepth = CreateScope<Pipeline>(m_Context);
        m_RenderPipelineNoDepth->BuildGraphics(vertSpv, fragSpv, cfg);
    }

    void DrawSystem::BeginFrame() {
        m_LineCount = 0;
        m_BoxCount = 0;
        m_SphereCount = 0;
        m_FrustumCount = 0;
        m_CrossCount = 0;
        m_DepthVertexCount = 0;
        m_NoDepthVertexCount = 0;
    }

    void DrawSystem::DispatchExpand(VkCommandBuffer cmd) {
        if (m_LineCount == 0 && m_BoxCount == 0 && m_SphereCount == 0 &&
            m_FrustumCount == 0 && m_CrossCount == 0) {
            return;
        }

        if (!m_ExpandPipeline) return;

        // Reset counters
        u32 zeros[2] = {0, 0};
        vkCmdUpdateBuffer(cmd, m_CounterBuffer->GetHandle(), 0, sizeof(zeros), zeros);

        // Barrier to ensure buffer updates are complete
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);

        // Dispatch compute shader to expand primitives
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ExpandPipeline->GetHandle());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ExpandPipeline->GetLayout(),
                                0, 1, &m_ComputeDescriptorSet, 0, nullptr);

        struct {
            u32 lineCount;
            u32 boxCount;
            u32 sphereCount;
            u32 frustumCount;
            u32 crossCount;
        } pushConstants;
        pushConstants.lineCount = m_LineCount;
        pushConstants.boxCount = m_BoxCount;
        pushConstants.sphereCount = m_SphereCount;
        pushConstants.frustumCount = m_FrustumCount;
        pushConstants.crossCount = m_CrossCount;

        vkCmdPushConstants(cmd, m_ExpandPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pushConstants), &pushConstants);

        u32 maxCount = m_LineCount + m_BoxCount + m_SphereCount + m_FrustumCount + m_CrossCount;
        u32 dispatchX = (maxCount + 255) / 256;
        vkCmdDispatch(cmd, dispatchX, 1, 1);

        // Barrier to ensure compute shader writes are complete
        VkMemoryBarrier computeBarrier{};
        computeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        computeBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0, 1, &computeBarrier, 0, nullptr, 0, nullptr);
    }

    void DrawSystem::Draw(VkCommandBuffer cmd, const Mat4 &viewProj,
                          VkImageView colorTarget,
                          VkImageView resolveColorTarget,
                          VkImageView depthTarget,
                          u32 width, u32 height,
                          bool useMsaaResolve) {
        if (m_LineCount == 0 && m_BoxCount == 0 && m_SphereCount == 0 &&
            m_FrustumCount == 0 && m_CrossCount == 0) {
            return;
        }

        if (!m_RenderPipeline) return;

        // Setup rendering attachments
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = colorTarget;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (useMsaaResolve) {
            colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            colorAttachment.resolveImageView = resolveColorTarget;
            colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = depthTarget;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = {{0, 0}, {width, height}};
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
        renderInfo.pDepthAttachment = &depthAttachment;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{{0, 0}, {width, height}};

        VkDeviceSize offset = 0;

        if (m_DepthVertexCount > 0) {
            vkCmdBeginRendering(cmd, &renderInfo);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_RenderPipeline->GetHandle());
            vkCmdPushConstants(cmd, m_RenderPipeline->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(Mat4), &viewProj);

            VkBuffer vertexBuffer = m_VertexBuffer->GetHandle();
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
            vkCmdDraw(cmd, m_DepthVertexCount, 1, 0, 0);
            vkCmdEndRendering(cmd);
        }

        // Draw without depth test
        if (m_RenderPipelineNoDepth &&m_NoDepthVertexCount 
        >
        0
        )
        {
            vkCmdBeginRendering(cmd, &renderInfo);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_RenderPipelineNoDepth->GetHandle());
            vkCmdPushConstants(cmd, m_RenderPipelineNoDepth->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(Mat4), &viewProj);

            VkBuffer vertexBufferNoDepth = m_VertexBufferNoDepth->GetHandle();
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBufferNoDepth, &offset);
            vkCmdDraw(cmd, m_NoDepthVertexCount, 1, 0, 0);
            vkCmdEndRendering(cmd);
        }
    }

    void DrawSystem::SubmitLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) {
        if (m_LineCount >= kMaxLines) return;
        u32 &vertexCount = depthTest ? m_DepthVertexCount : m_NoDepthVertexCount;
        if (vertexCount + 2 > kMaxVertices) return;

        DrawLineCmd cmd;
        cmd.start = a;
        cmd.end = b;
        cmd.color = color;
        cmd.depthTest = depthTest ? 1u : 0u;

        m_LineCommandBuffer->LoadData(&cmd, sizeof(DrawLineCmd), sizeof(DrawLineCmd) * m_LineCount);
        m_LineCount++;
        vertexCount += 2;
    }

    void DrawSystem::SubmitBox(const Vec3 &center, const Vec3 &halfExtents, const Mat4 &transform, u32 color,
                               bool depthTest) {
        if (m_BoxCount >= kMaxBoxes) return;
        u32 &vertexCount = depthTest ? m_DepthVertexCount : m_NoDepthVertexCount;
        if (vertexCount + 24 > kMaxVertices) return;

        DrawBoxCmd cmd;
        cmd.center = center;
        cmd.halfExtents = halfExtents;
        cmd.transform = transform;
        cmd.color = color;
        cmd.depthTest = depthTest ? 1u : 0u;

        m_BoxCommandBuffer->LoadData(&cmd, sizeof(DrawBoxCmd), sizeof(DrawBoxCmd) * m_BoxCount);
        m_BoxCount++;
        vertexCount += 24;
    }

    void DrawSystem::SubmitSphere(const Vec3 &center, float radius, u32 color, int segments, bool depthTest) {
        if (m_SphereCount >= kMaxSpheres) return;
        u32 &vertexCount = depthTest ? m_DepthVertexCount : m_NoDepthVertexCount;
        if (vertexCount + 48 > kMaxVertices) return;

        DrawSphereCmd cmd;
        cmd.center = center;
        cmd.radius = radius;
        cmd.color = color;
        cmd.segments = static_cast<u32>(segments);
        cmd.depthTest = depthTest ? 1u : 0u;
        cmd._pad0 = 0;

        m_SphereCommandBuffer->LoadData(&cmd, sizeof(DrawSphereCmd), sizeof(DrawSphereCmd) * m_SphereCount);
        m_SphereCount++;
        vertexCount += 48;
    }

    void DrawSystem::SubmitFrustum(const Mat4 &invViewProj, u32 color, bool depthTest) {
        if (m_FrustumCount >= kMaxFrustums) return;
        u32 &vertexCount = depthTest ? m_DepthVertexCount : m_NoDepthVertexCount;
        if (vertexCount + 24 > kMaxVertices) return;

        DrawFrustumCmd cmd;
        cmd.invViewProj = invViewProj;
        cmd.color = color;
        cmd.depthTest = depthTest ? 1u : 0u;
        cmd._pad0 = 0;
        cmd._pad1 = 0;

        m_FrustumCommandBuffer->LoadData(&cmd, sizeof(DrawFrustumCmd), sizeof(DrawFrustumCmd) * m_FrustumCount);
        m_FrustumCount++;
        vertexCount += 24;
    }

    void DrawSystem::SubmitCross(const Vec3 &center, float size, u32 color, bool depthTest) {
        if (m_CrossCount >= kMaxCrosses) return;
        u32 &vertexCount = depthTest ? m_DepthVertexCount : m_NoDepthVertexCount;
        if (vertexCount + 6 > kMaxVertices) return;

        DrawCrossCmd cmd;
        cmd.center = center;
        cmd.size = size;
        cmd.color = color;
        cmd.depthTest = depthTest ? 1u : 0u;

        m_CrossCommandBuffer->LoadData(&cmd, sizeof(DrawCrossCmd), sizeof(DrawCrossCmd) * m_CrossCount);
        m_CrossCount++;
        vertexCount += 6;
    }
} // namespace Manro