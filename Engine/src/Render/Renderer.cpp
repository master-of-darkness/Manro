#include "Renderer.h"
#include <Render/Vulkan/VulkanHelpers.h>
#include <Core/Logger.h>
#include <stdexcept>

struct MeshPushConstants {
    Engine::Mat4 mvp;
    Engine::Vec3 tint;
    float _pad{0.0f};
};

static_assert(sizeof(MeshPushConstants) % 16 == 0);

namespace Engine {
    Renderer::Renderer(IWindow &window, u32 width, u32 height,
                       VkSampleCountFlagBits msaaSamples)
        : m_Context("GameEngine", window),
          m_Textures(m_Context),
          m_Meshes(m_Context) {
        m_Swapchain = CreateScope<Swapchain>(m_Context, width, height);

        VkSampleCountFlagBits maxSamples = m_Context.GetMaxUsableSampleCount();
        m_MsaaSamples = (static_cast<u32>(msaaSamples) <= static_cast<u32>(maxSamples))
                            ? msaaSamples : maxSamples;
        LOG_INFO("[Renderer] MSAA samples: {} (requested: {}, max: {})",
                 (int)m_MsaaSamples, (int)msaaSamples, (int)maxSamples);

        CreateColorResources(width, height);
        CreateDepthResources(width, height);
        CreateCommandBuffers();
        CreateSyncObjects();
        LoadShadersAndPipeline();

        if (!m_DefaultMaterial) {
            throw std::runtime_error("[Renderer] Failed to load default material (shaders missing?)");
        }

        m_PendingWidth = width;
        m_PendingHeight = height;
    }

    Renderer::~Renderer() {
        if (!m_Context.GetDevice()) return;
        vkDeviceWaitIdle(m_Context.GetDevice());

        DestroyImage(m_Context, m_ColorImage);
        DestroyImage(m_Context, m_DepthImage);

        m_DefaultMaterial.reset();

        for (auto &frame: m_Frames) {
            if (frame.renderFinishedSemaphore)
                vkDestroySemaphore(m_Context.GetDevice(), frame.renderFinishedSemaphore, nullptr);
            if (frame.imageAvailableSemaphore)
                vkDestroySemaphore(m_Context.GetDevice(), frame.imageAvailableSemaphore, nullptr);
            if (frame.inFlightFence)
                vkDestroyFence(m_Context.GetDevice(), frame.inFlightFence, nullptr);
            if (frame.commandPool)
                vkDestroyCommandPool(m_Context.GetDevice(), frame.commandPool, nullptr);
        }

        if (m_Swapchain) m_Swapchain->Shutdown();
    }

    void Renderer::OnResize(u32 width, u32 height) {
        m_PendingWidth = width;
        m_PendingHeight = height;
        m_PendingResize = true;
    }

    void Renderer::RecreateSwapchain() {
        const u32 w = m_PendingWidth;
        const u32 h = m_PendingHeight;
        m_PendingResize = false;

        if (w == 0 || h == 0) return;

        m_Swapchain->Recreate(w, h);

        DestroyImage(m_Context, m_ColorImage);
        CreateColorResources(w, h);

        DestroyImage(m_Context, m_DepthImage);
        CreateDepthResources(w, h);

        LOG_INFO("[Renderer] Swapchain recreated {}x{}", w, h);
    }

    void Renderer::CreateDepthResources(u32 width, u32 height) {
        ImageCreateParams params{};
        params.width = width;
        params.height = height;
        params.format = m_DepthFormat;
        params.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        params.samples = m_MsaaSamples;
        m_DepthImage = CreateImage(m_Context, params, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    void Renderer::CreateColorResources(u32 width, u32 height) {
        if (m_MsaaSamples == VK_SAMPLE_COUNT_1_BIT) return;

        ImageCreateParams params{};
        params.width = width;
        params.height = height;
        params.format = m_Swapchain->GetImageFormat();
        params.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        params.samples = m_MsaaSamples;
        m_ColorImage = CreateImage(m_Context, params);
    }

    bool Renderer::BeginFrame() {
        if (m_PendingResize || m_Swapchain->NeedsRecreate()) {
            RecreateSwapchain();
            return false;
        }

        if (m_PendingWidth == 0 || m_PendingHeight == 0)
            return false;

        FrameData &frame = m_Frames[m_CurrentFrame];

        vkWaitForFences(m_Context.GetDevice(), 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

        m_CurrentImageIndex = m_Swapchain->AcquireNextImage(frame.imageAvailableSemaphore);
        if (m_CurrentImageIndex == UINT32_MAX) {
            return false;
        }

        vkResetFences(m_Context.GetDevice(), 1, &frame.inFlightFence);
        vkResetCommandBuffer(frame.commandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin recording command buffer!");

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.image = m_Swapchain->GetImage(m_CurrentImageIndex);
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkImageMemoryBarrier2 barriers[2];
        u32 barrierCount = 1;
        barriers[0] = barrier;

        if (m_MsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
            VkImageMemoryBarrier2 msaaBarrier{};
            msaaBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            msaaBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            msaaBarrier.srcAccessMask = 0;
            msaaBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            msaaBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            msaaBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            msaaBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            msaaBarrier.image = m_ColorImage.image;
            msaaBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[1] = msaaBarrier;
            barrierCount = 2;
        }

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = barrierCount;
        dep.pImageMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(frame.commandBuffer, &dep);

        return true;
    }

    void Renderer::BeginRenderPass(Vec4 clearColor) {
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;

        VkClearValue cv{};
        cv.color = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.clearValue = cv;

        if (m_MsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
            colorAttachment.imageView = m_ColorImage.view;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            colorAttachment.resolveImageView = m_Swapchain->GetImageView(m_CurrentImageIndex);
            colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        } else {
            colorAttachment.imageView = m_Swapchain->GetImageView(m_CurrentImageIndex);
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = m_DepthImage.view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea.offset = {0, 0};
        renderInfo.renderArea.extent = m_Swapchain->GetExtent();
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
        renderInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cb, &renderInfo);

        VkViewport viewport{};
        viewport.width = static_cast<float>(m_Swapchain->GetExtent().width);
        viewport.height = static_cast<float>(m_Swapchain->GetExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = m_Swapchain->GetExtent();
        vkCmdSetScissor(cb, 0, 1, &scissor);

        m_Textures.ResetBinding();
    }

    void Renderer::EndRenderPass() {
        vkCmdEndRendering(m_Frames[m_CurrentFrame].commandBuffer);
    }

    void Renderer::EndFrameAndPresent() {
        FrameData &frame = m_Frames[m_CurrentFrame];

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.image = m_Swapchain->GetImage(m_CurrentImageIndex);
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(frame.commandBuffer, &dep);

        if (vkEndCommandBuffer(frame.commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to record command buffer!");

        VkCommandBufferSubmitInfo cmdInfo{};
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdInfo.commandBuffer = frame.commandBuffer;

        VkSemaphoreSubmitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = frame.imageAvailableSemaphore;
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSemaphoreSubmitInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = frame.renderFinishedSemaphore;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &waitInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &signalInfo;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdInfo;

        if (vkQueueSubmit2(m_Context.GetGraphicsQueue(), 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS)
            throw std::runtime_error("Failed to submit draw command buffer!");

        bool needsRecreate = m_Swapchain->Present(m_CurrentImageIndex, frame.renderFinishedSemaphore);
        if (needsRecreate)
            m_PendingResize = true;

        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::DrawMesh(MeshHandle meshId, const Mat4 &mvp) {
        const auto *mesh = m_Meshes.Get(meshId);
        if (!mesh) return;

        VkCommandBuffer cb = m_Frames[m_CurrentFrame].commandBuffer;

        MeshPushConstants pc;
        pc.mvp = mvp;
        pc.tint = m_TintColor;

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DefaultMaterial->GetHandle());
        vkCmdPushConstants(cb, m_DefaultMaterial->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &pc);
        VkDescriptorSet ds = m_Textures.GetActiveDescriptorSet();
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_DefaultMaterial->GetLayout(), 0, 1, &ds, 0, nullptr);

        VkBuffer vb[] = {mesh->vertexBuffer->GetHandle()};
        VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vb, offs);
        vkCmdBindIndexBuffer(cb, mesh->indexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, mesh->indexCount, 1, 0, 0, 0);
    }

    void Renderer::DrawMesh(MeshHandle meshId, const MaterialInstance &material, const Mat4 &mvp) {
        const auto *mesh = m_Meshes.Get(meshId);
        if (!mesh) return;

        VkCommandBuffer cb = m_Frames[m_CurrentFrame].commandBuffer;

        MeshPushConstants pc;
        pc.mvp = mvp;
        pc.tint = material.GetTintColor();

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, material.GetMaterial().GetHandle());
        vkCmdPushConstants(cb, material.GetMaterial().GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &pc);

        // Bind material texture
        m_Textures.Bind(material.GetTexture());
        VkDescriptorSet ds = m_Textures.GetActiveDescriptorSet();
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                material.GetMaterial().GetLayout(), 0, 1, &ds, 0, nullptr);

        VkBuffer vb[] = {mesh->vertexBuffer->GetHandle()};
        VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vb, offs);
        vkCmdBindIndexBuffer(cb, mesh->indexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, mesh->indexCount, 1, 0, 0, 0);
    }

    void Renderer::CreateCommandBuffers() {
        m_Frames.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_Context.GetGraphicsQueueFamilyIndex();

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateCommandPool(m_Context.GetDevice(), &poolInfo, nullptr, &m_Frames[i].commandPool) != VK_SUCCESS)
                throw std::runtime_error("Failed to create command pool!");

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = m_Frames[i].commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            if (vkAllocateCommandBuffers(m_Context.GetDevice(), &allocInfo, &m_Frames[i].commandBuffer) != VK_SUCCESS)
                throw std::runtime_error("Failed to allocate command buffers!");
        }
    }

    void Renderer::CreateSyncObjects() {
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateSemaphore(m_Context.GetDevice(), &semInfo, nullptr, &m_Frames[i].imageAvailableSemaphore) !=
                VK_SUCCESS ||
                vkCreateSemaphore(m_Context.GetDevice(), &semInfo, nullptr, &m_Frames[i].renderFinishedSemaphore) !=
                VK_SUCCESS ||
                vkCreateFence(m_Context.GetDevice(), &fenceInfo, nullptr, &m_Frames[i].inFlightFence) != VK_SUCCESS)
                throw std::runtime_error("Failed to create sync objects!");
        }
    }

    void Renderer::LoadShadersAndPipeline() {
        std::vector<u8> vertSpv = ReadBinaryFile("assets/shaders/cube.vert.spv");
        std::vector<u8> fragSpv = ReadBinaryFile("assets/shaders/cube.frag.spv");

        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] Failed to load precompiled shaders!");
            // Fallback to runtime compilation if needed, but let's assume precompilation is mandatory now
            return;
        }

        auto pipeline = CreateScope<Pipeline>(m_Context);
        PipelineConfigParams config{};
        config.colorAttachmentFormat = m_Swapchain->GetImageFormat();
        config.depthAttachmentFormat = m_DepthFormat;
        config.descriptorSetLayout = m_Textures.GetDescriptorSetLayout();

        config.vertexInputBindings.resize(1);
        config.vertexInputBindings[0].binding = 0;
        config.vertexInputBindings[0].stride = sizeof(Vertex);
        config.vertexInputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        config.vertexInputAttributes.resize(3);
        config.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        config.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};
        config.vertexInputAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};

        config.pushConstantSize = sizeof(MeshPushConstants);
        config.msaaSamples = m_MsaaSamples;

        pipeline->BuildGraphics(vertSpv, fragSpv, config);
        m_DefaultMaterial = CreateRef<Material>(std::move(pipeline));
    }
} // namespace Engine
