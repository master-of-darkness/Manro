#include "Renderer.h"
#include <Core/Logger.h>
#include <stdexcept>
#include <cstring>

struct MeshPushConstants {
    Engine::Mat4 mvp;
    Engine::Vec3 tint;
    float _pad{0.0f};
};

static_assert(sizeof(MeshPushConstants) % 16 == 0);

struct PBRPushConstants {
    Engine::Mat4 mvp;
    Engine::Mat4 model;
};

static_assert(sizeof(PBRPushConstants) % 16 == 0);

namespace Engine {
    Renderer::Renderer() = default;

    Renderer::~Renderer() {
        Shutdown();
    }

    void Renderer::Initialize(IWindow *window, u32 width, u32 height,
                              VkSampleCountFlagBits msaaSamples) {
        m_Context.Initialize("GameEngine");
        m_Context.CreateSurface(window);

        m_Swapchain = CreateScope<Swapchain>(m_Context);
        m_Swapchain->Initialize(width, height);

        VkSampleCountFlagBits maxSamples = m_Context.GetMaxUsableSampleCount();
        VkSampleCountFlagBits effectiveMsaa = (static_cast<u32>(msaaSamples) <= static_cast<u32>(maxSamples))
                            ? msaaSamples : maxSamples;
        LOG_INFO("[Renderer] MSAA samples: {} (requested: {}, max: {})",
                 (int)effectiveMsaa, (int)msaaSamples, (int)maxSamples);

        m_RenderTargets.Initialize(m_Context, *m_Swapchain, width, height, effectiveMsaa);
        m_FrameManager.Initialize(m_Context);
        m_Descriptors.Initialize(m_Context);

        m_Resources.Initialize(m_Context, m_Descriptors.GetPool(),
                               m_Descriptors.GetTextureLayout(),
                               m_Descriptors.GetDefaultSampler());

        LoadShadersAndPipeline();
        m_Resources.CreateDefaultTextures();
        CreatePBRPipeline();

        const auto* whiteTex = m_Resources.GetTexture(m_Resources.GetWhiteTextureId());
        m_ActiveDescriptorSet = whiteTex ? whiteTex->descriptorSet : VK_NULL_HANDLE;

        m_PendingWidth = width;
        m_PendingHeight = height;
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
        m_RenderTargets.Recreate(m_Context.GetDevice(), m_Context.GetAllocator(),
                                 *m_Swapchain, w, h);

        LOG_INFO("[Renderer] Swapchain recreated {}x{}", w, h);
    }

    bool Renderer::BeginFrame() {
        if (m_PendingResize || m_Swapchain->NeedsRecreate()) {
            RecreateSwapchain();
            return false;
        }

        if (m_PendingWidth == 0 || m_PendingHeight == 0)
            return false;

        FrameData &frame = m_FrameManager.CurrentFrame();

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

        if (m_RenderTargets.GetMsaaSamples() != VK_SAMPLE_COUNT_1_BIT) {
            VkImageMemoryBarrier2 msaaBarrier{};
            msaaBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            msaaBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            msaaBarrier.srcAccessMask = 0;
            msaaBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            msaaBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            msaaBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            msaaBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            msaaBarrier.image = m_RenderTargets.GetColorImage();
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
        FrameData &frame = m_FrameManager.CurrentFrame();
        VkCommandBuffer cb = frame.commandBuffer;

        VkClearValue cv{};
        cv.color = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.clearValue = cv;

        if (m_RenderTargets.GetMsaaSamples() != VK_SAMPLE_COUNT_1_BIT) {
            colorAttachment.imageView = m_RenderTargets.GetColorView();
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
        depthAttachment.imageView = m_RenderTargets.GetDepthView();
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
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());

        VkViewport viewport{};
        viewport.width = static_cast<float>(m_Swapchain->GetExtent().width);
        viewport.height = static_cast<float>(m_Swapchain->GetExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = m_Swapchain->GetExtent();
        vkCmdSetScissor(cb, 0, 1, &scissor);

        const auto* whiteTex = m_Resources.GetTexture(m_Resources.GetWhiteTextureId());
        m_ActiveDescriptorSet = whiteTex ? whiteTex->descriptorSet : VK_NULL_HANDLE;
    }

    void Renderer::EndRenderPass() {
        vkCmdEndRendering(m_FrameManager.CurrentFrame().commandBuffer);
    }

    void Renderer::EndFrameAndPresent() {
        FrameData &frame = m_FrameManager.CurrentFrame();

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

        m_FrameManager.AdvanceFrame();
    }

    void Renderer::Shutdown() {
        if (!m_Context.GetDevice()) return;
        vkDeviceWaitIdle(m_Context.GetDevice());

        m_Resources.Shutdown(m_Context.GetDevice(), m_Context.GetAllocator());
        m_RenderTargets.Shutdown(m_Context.GetDevice(), m_Context.GetAllocator());

        m_LightUBO.reset();
        m_PBRPipeline.reset();

        m_Descriptors.Shutdown(m_Context.GetDevice());

        m_Pipeline.reset();
        m_ShaderCompiler.reset();
        m_VertexBuffer.reset();
        m_IndexBuffer.reset();

        m_FrameManager.Shutdown(m_Context.GetDevice());

        if (m_Swapchain) m_Swapchain->Shutdown();
        m_Context.Shutdown();
    }

    void Renderer::LoadShadersAndPipeline() {
        m_ShaderCompiler = CreateScope<SlangCompiler>();
        m_ShaderCompiler->Initialize();

        std::vector<u8> vertSpv, fragSpv;
        if (!m_ShaderCompiler->CompileShaderToSPIRV("assets/shaders/cube.slang", "vertexMain", "sm_6_5", vertSpv)) {
            LOG_ERROR("[Renderer] Failed to compile vertex shader!");
            return;
        }
        if (!m_ShaderCompiler->CompileShaderToSPIRV("assets/shaders/cube.slang", "fragmentMain", "sm_6_5", fragSpv)) {
            LOG_ERROR("[Renderer] Failed to compile fragment shader!");
            return;
        }

        m_Pipeline = CreateScope<Pipeline>(m_Context);
        PipelineConfigParams config{};
        config.colorAttachmentFormat = m_Swapchain->GetImageFormat();
        config.depthAttachmentFormat = m_RenderTargets.GetDepthFormat();
        config.descriptorSetLayout = m_Descriptors.GetTextureLayout();

        config.vertexInputBindings.resize(1);
        config.vertexInputBindings[0].binding = 0;
        config.vertexInputBindings[0].stride = sizeof(Vertex);
        config.vertexInputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        config.vertexInputAttributes.resize(3);
        config.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        config.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};
        config.vertexInputAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};

        config.pushConstantSize = sizeof(MeshPushConstants);
        config.msaaSamples = m_RenderTargets.GetMsaaSamples();

        m_Pipeline->BuildGraphics(vertSpv, fragSpv, config);

        struct OldVertex {
            Vec3 pos;
            Vec3 col;
            Vec2 uv;
        };
        std::vector<OldVertex> vertices = {
            {{-0.5f, -0.5f, -0.5f}, {0.75f, 0.75f, 0.75f}, {0, 0}},
            {{0.5f, -0.5f, -0.5f}, {0.75f, 0.75f, 0.75f}, {1, 0}},
            {{0.5f, 0.5f, -0.5f}, {0.65f, 0.65f, 0.65f}, {1, 1}}, {{-0.5f, 0.5f, -0.5f}, {0.65f, 0.65f, 0.65f}, {0, 1}},
            {{-0.5f, -0.5f, 0.5f}, {0.80f, 0.80f, 0.80f}, {0, 0}}, {{0.5f, -0.5f, 0.5f}, {0.80f, 0.80f, 0.80f}, {1, 0}},
            {{0.5f, 0.5f, 0.5f}, {0.70f, 0.70f, 0.70f}, {1, 1}}, {{-0.5f, 0.5f, 0.5f}, {0.70f, 0.70f, 0.70f}, {0, 1}},
        };
        std::vector<u32> indices = {
            0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5,
            4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4
        };

        m_VertexBuffer = CreateScope<Buffer>(m_Context, sizeof(OldVertex) * vertices.size(),
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_VertexBuffer->LoadData(vertices.data(), sizeof(OldVertex) * vertices.size());

        m_IndexBuffer = CreateScope<Buffer>(m_Context, sizeof(u32) * indices.size(),
                                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_IndexBuffer->LoadData(indices.data(), sizeof(u32) * indices.size());
    }

    void Renderer::CreatePBRPipeline() {
        m_Descriptors.CreatePBRDescriptorLayouts(m_Context.GetDevice());

        m_LightUBO = CreateScope<Buffer>(m_Context, sizeof(LightDataGPU),
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_Descriptors.AllocateLightDescriptorSet(m_Context.GetDevice(), *m_LightUBO);

        std::vector<u8> vertSpv, fragSpv;
        if (!m_ShaderCompiler->CompileShaderToSPIRV("assets/shaders/pbr.slang",
                                                     "vertexMain", "sm_6_5", vertSpv)) {
            LOG_ERROR("[Renderer] Failed to compile PBR vertex shader!");
            return;
        }
        if (!m_ShaderCompiler->CompileShaderToSPIRV("assets/shaders/pbr.slang",
                                                     "fragmentMain", "sm_6_5", fragSpv)) {
            LOG_ERROR("[Renderer] Failed to compile PBR fragment shader!");
            return;
        }

        m_PBRPipeline = CreateScope<Pipeline>(m_Context);
        PipelineConfigParams config{};
        config.colorAttachmentFormat = m_Swapchain->GetImageFormat();
        config.depthAttachmentFormat = m_RenderTargets.GetDepthFormat();
        config.descriptorSetLayouts = {m_Descriptors.GetPBRLightLayout(),
                                       m_Descriptors.GetPBRMaterialLayout()};

        config.vertexInputBindings.resize(1);
        config.vertexInputBindings[0].binding = 0;
        config.vertexInputBindings[0].stride = sizeof(PBRVertex);
        config.vertexInputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        config.vertexInputAttributes.resize(4);
        config.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                            offsetof(PBRVertex, position)};
        config.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                            offsetof(PBRVertex, normal)};
        config.vertexInputAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,
                                            offsetof(PBRVertex, uv)};
        config.vertexInputAttributes[3] = {3, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                            offsetof(PBRVertex, tangent)};

        config.pushConstantSize = sizeof(PBRPushConstants);
        config.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
        config.msaaSamples = m_RenderTargets.GetMsaaSamples();

        m_PBRPipeline->BuildGraphics(vertSpv, fragSpv, config);
        LOG_INFO("[Renderer] PBR pipeline created.");
    }

    u32 Renderer::UploadMesh(const ModelData &data) {
        return m_Resources.UploadMesh(data);
    }

    u32 Renderer::UploadPBRMesh(const PBRSubMeshData &data) {
        return m_Resources.UploadPBRMesh(data);
    }

    u32 Renderer::UploadTexture(const TextureData &data) {
        return m_Resources.UploadTexture(data);
    }

    void Renderer::BindTexture(u32 textureId) {
        const auto* whiteTex = m_Resources.GetTexture(m_Resources.GetWhiteTextureId());
        if (textureId == 0) {
            m_ActiveDescriptorSet = whiteTex ? whiteTex->descriptorSet : VK_NULL_HANDLE;
            return;
        }
        const auto* tex = m_Resources.GetTexture(textureId);
        m_ActiveDescriptorSet = tex ? tex->descriptorSet
                                    : (whiteTex ? whiteTex->descriptorSet : VK_NULL_HANDLE);
    }

    void Renderer::DrawMesh(u32 meshId, const Mat4 &mvp) {
        const auto* mesh = m_Resources.GetMesh(meshId);
        if (!mesh) return;

        VkCommandBuffer cb = m_FrameManager.CurrentFrame().commandBuffer;

        MeshPushConstants pc;
        pc.mvp = mvp;
        pc.tint = m_TintColor;

        vkCmdPushConstants(cb, m_Pipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &pc);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_Pipeline->GetLayout(), 0, 1, &m_ActiveDescriptorSet, 0, nullptr);

        VkBuffer vb[] = {mesh->vertexBuffer->GetHandle()};
        VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vb, offs);
        vkCmdBindIndexBuffer(cb, mesh->indexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, mesh->indexCount, 1, 0, 0, 0);
    }

    void Renderer::UpdateLights(const LightDataGPU &lightData) {
        m_LightUBO->LoadData(&lightData, sizeof(LightDataGPU));
    }

    void Renderer::BeginPBRPass() {
        if (!m_PBRPipeline || !m_PBRPipeline->GetHandle()) return;

        VkCommandBuffer cb = m_FrameManager.CurrentFrame().commandBuffer;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PBRPipeline->GetHandle());

        VkViewport viewport{};
        viewport.width = static_cast<float>(m_Swapchain->GetExtent().width);
        viewport.height = static_cast<float>(m_Swapchain->GetExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = m_Swapchain->GetExtent();
        vkCmdSetScissor(cb, 0, 1, &scissor);

        VkDescriptorSet lightSet = m_Descriptors.GetLightDescriptorSet();
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_PBRPipeline->GetLayout(), 0, 1,
                                &lightSet, 0, nullptr);
    }

    void Renderer::BuildPBRMaterialDescriptor(PBRMaterial &mat) {
        m_Descriptors.BuildPBRMaterialDescriptor(m_Context.GetDevice(), mat, m_Resources);
    }

    void Renderer::BindPBRMaterial(const PBRMaterial &mat) {
        if (mat.descriptorSet == VK_NULL_HANDLE) return;

        VkCommandBuffer cb = m_FrameManager.CurrentFrame().commandBuffer;
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_PBRPipeline->GetLayout(), 1, 1,
                                &mat.descriptorSet, 0, nullptr);
    }

    void Renderer::DrawPBRMesh(u32 meshId, const Mat4 &mvp, const Mat4 &model) {
        const auto* mesh = m_Resources.GetMesh(meshId);
        if (!mesh) return;

        VkCommandBuffer cb = m_FrameManager.CurrentFrame().commandBuffer;

        PBRPushConstants pc;
        pc.mvp = mvp;
        pc.model = model;

        vkCmdPushConstants(cb, m_PBRPipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(PBRPushConstants), &pc);

        VkBuffer vb[] = {mesh->vertexBuffer->GetHandle()};
        VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vb, offs);
        vkCmdBindIndexBuffer(cb, mesh->indexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, mesh->indexCount, 1, 0, 0, 0);
    }

    void Renderer::DrawCube(const Mat4 &mvp) {
        if (!m_Pipeline || !m_Pipeline->GetHandle()) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                LOG_ERROR("[Renderer::DrawCube] Pipeline is null!");
            }
            return;
        }

        VkCommandBuffer cb = m_FrameManager.CurrentFrame().commandBuffer;

        vkCmdPushConstants(cb, m_Pipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &mvp);

        const auto* whiteTex = m_Resources.GetTexture(m_Resources.GetWhiteTextureId());
        VkDescriptorSet whiteSet = whiteTex ? whiteTex->descriptorSet : VK_NULL_HANDLE;
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_Pipeline->GetLayout(), 0, 1,
                                &whiteSet, 0, nullptr);

        VkBuffer vb[] = {m_VertexBuffer->GetHandle()};
        VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vb, offs);
        vkCmdBindIndexBuffer(cb, m_IndexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, 36, 1, 0, 0, 0);
    }
} // namespace Engine
