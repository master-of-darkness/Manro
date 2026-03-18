#include <Manro/Render/Renderer.h>
#include <Manro/Render/Vulkan/VulkanHelpers.h>
#include <Manro/Core/Logger.h>
#include <Manro/Render/Model.h>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace Manro {
    static constexpr u32 kMaxTilesX = 120u;
    static constexpr u32 kMaxTilesY = 68u;
    static constexpr u32 kMaxTiles = kMaxTilesX * kMaxTilesY;

    Renderer::Renderer(IWindow &window, u32 width, u32 height,
                       VkSampleCountFlagBits msaaSamples)
        : m_Context("GameEngine", window),
          m_Textures(m_Context),
          m_Meshes(m_Context) {
        m_Swapchain = CreateScope<Swapchain>(m_Context, width, height);

        VkSampleCountFlagBits maxSamples = m_Context.GetMaxUsableSampleCount();
        m_MsaaSamples = (static_cast<u32>(msaaSamples) <= static_cast<u32>(maxSamples))
                            ? msaaSamples
                            : maxSamples;

        m_PendingWidth = width;
        m_PendingHeight = height;

        CreateOffscreenResources(width, height);
        CreateColorResources(width, height);
        CreateDepthResources(width, height);
        CreateDescriptorLayouts();
        CreateDescriptorPool();
        CreateGpuBuffers();
        BuildPbrPipeline();
        BuildCompositePipeline();
        BuildCullPipeline();
        CreateCommandBuffers();
        CreateSyncObjects();

        m_Materials.push_back(shaderio::defaultGltfMaterial());
        m_MaterialBuffer->LoadData(m_Materials.data(), sizeof(MaterialData));
    }

    Renderer::~Renderer() {
        if (!m_Context.GetDevice()) return;
        vkDeviceWaitIdle(m_Context.GetDevice());

        m_DefaultMaterial.reset();
        m_PbrPipeline.reset();
        m_CompositePipeline.reset();
        m_CullPipeline.reset();

        if (m_OffscreenSampler)
            vkDestroySampler(m_Context.GetDevice(), m_OffscreenSampler, nullptr);
        DestroyImage(m_Context, m_OffscreenColor);
        DestroyImage(m_Context, m_MsaaColorImage);
        DestroyImage(m_Context, m_DepthImage);

        for (auto &f: m_Frames)
            if (f.commandPool)
                vkDestroyCommandPool(m_Context.GetDevice(), f.commandPool, nullptr);

        for (auto s: m_ImageAvailableSemaphores)
            vkDestroySemaphore(m_Context.GetDevice(), s, nullptr);
        for (auto s: m_PresentSemaphores)
            vkDestroySemaphore(m_Context.GetDevice(), s, nullptr);
        if (m_TimelineSemaphore)
            vkDestroySemaphore(m_Context.GetDevice(), m_TimelineSemaphore, nullptr);

        if (m_DescriptorPool)
            vkDestroyDescriptorPool(m_Context.GetDevice(), m_DescriptorPool, nullptr);
        if (m_PbrSetLayout)
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_PbrSetLayout, nullptr);
        if (m_CompositeSetLayout)
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_CompositeSetLayout, nullptr);
        if (m_CullSetLayout)
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_CullSetLayout, nullptr);

        if (m_Swapchain) m_Swapchain->Shutdown();
    }

    void Renderer::AddLight(const LightData &light) {
        if (m_PendingLights.size() < MAX_LIGHTS)
            m_PendingLights.push_back(light);
    }

    void Renderer::ClearLights() { m_PendingLights.clear(); }

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

        if (m_TimelineValue > 0) {
            VkSemaphoreWaitInfo wi{};
            wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wi.semaphoreCount = 1;
            wi.pSemaphores = &m_TimelineSemaphore;
            wi.pValues = &m_TimelineValue;
            vkWaitSemaphores(m_Context.GetDevice(), &wi, UINT64_MAX);
        }

        m_Swapchain->Recreate(w, h);

        if (m_PresentSemaphores.size() != m_Swapchain->GetImageCount()) {
            for (auto s: m_PresentSemaphores)
                vkDestroySemaphore(m_Context.GetDevice(), s, nullptr);
            VkSemaphoreCreateInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            m_PresentSemaphores.resize(m_Swapchain->GetImageCount());
            for (auto &s: m_PresentSemaphores)
                if (vkCreateSemaphore(m_Context.GetDevice(), &si, nullptr, &s) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create present semaphore");
        }

        if (m_OffscreenSampler) {
            vkDestroySampler(m_Context.GetDevice(), m_OffscreenSampler, nullptr);
            m_OffscreenSampler = VK_NULL_HANDLE;
        }
        DestroyImage(m_Context, m_OffscreenColor);
        DestroyImage(m_Context, m_MsaaColorImage);
        DestroyImage(m_Context, m_DepthImage);

        CreateOffscreenResources(w, h);
        CreateColorResources(w, h);
        CreateDepthResources(w, h);

        for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            UpdateCompositeDescriptorSet(i);
    }

    void Renderer::CreateOffscreenResources(u32 width, u32 height) {
        ImageCreateParams p{};
        p.width = width;
        p.height = height;
        p.format = m_OffscreenFormat;
        p.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        p.samples = VK_SAMPLE_COUNT_1_BIT;
        m_OffscreenColor = CreateImage(m_Context, p);

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = VK_LOD_CLAMP_NONE;
        if (vkCreateSampler(m_Context.GetDevice(), &si, nullptr, &m_OffscreenSampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create offscreen sampler");

        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_OffscreenColor.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        });
    }

    void Renderer::CreateDepthResources(u32 width, u32 height) {
        ImageCreateParams p{};
        p.width = width;
        p.height = height;
        p.format = m_DepthFormat;
        p.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        p.samples = m_MsaaSamples;
        m_DepthImage = CreateImage(m_Context, p, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    void Renderer::CreateColorResources(u32 width, u32 height) {
        if (m_MsaaSamples == VK_SAMPLE_COUNT_1_BIT) return;
        ImageCreateParams p{};
        p.width = width;
        p.height = height;
        p.format = m_OffscreenFormat;
        p.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        p.samples = m_MsaaSamples;
        m_MsaaColorImage = CreateImage(m_Context, p);
    }

    bool Renderer::BeginFrame() {
        if (m_PendingResize || m_Swapchain->NeedsRecreate()) {
            RecreateSwapchain();
            return false;
        }
        if (m_PendingWidth == 0 || m_PendingHeight == 0) return false;

        const u64 waitValue = m_FrameBaseValue[m_CurrentFrame];
        VkSemaphoreWaitInfo wi{};
        wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wi.semaphoreCount = 1;
        wi.pSemaphores = &m_TimelineSemaphore;
        wi.pValues = &waitValue;
        vkWaitSemaphores(m_Context.GetDevice(), &wi, UINT64_MAX);

        m_CurrentImageIndex = m_Swapchain->AcquireNextImage(
            m_ImageAvailableSemaphores[m_CurrentFrame]);
        if (m_CurrentImageIndex == UINT32_MAX) return false;

        FrameData &frame = m_Frames[m_CurrentFrame];
        vkResetCommandBuffer(frame.commandBuffer, 0);
        m_CurrentFrameInstances.clear();

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(frame.commandBuffer, &bi) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin command buffer");

        UploadLights(m_CurrentFrame);

        return true;
    }

    void Renderer::UploadLights(u32 frameIndex) {
        FrameData &frame = m_Frames[frameIndex];
        if (!m_PendingLights.empty())
            frame.lightBuffer->LoadData(
                m_PendingLights.data(),
                sizeof(LightData) * m_PendingLights.size());
    }

    void Renderer::BeginRendering(Vec4 clearColor) {
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;
        VkExtent2D ext = m_Swapchain->GetExtent();

        if (!m_CurrentFrameInstances.empty()) {
            u32 instanceCount = (u32)m_CurrentFrameInstances.size();
            frame.instanceBuffer->LoadData(
                m_CurrentFrameInstances.data(),
                sizeof(GpuMeshInstance) * instanceCount);

            std::vector<GpuDrawCommand> cmds;
            cmds.reserve(instanceCount);
            for (u32 i = 0; i < instanceCount; ++i) {
                auto &inst = m_CurrentFrameInstances[i];
                GpuDrawCommand cmd{};
                cmd.indexCount = inst.indexCount;
                cmd.instanceCount = 1;
                cmd.firstIndex = inst.firstIndex;
                cmd.vertexOffset = (int)inst.firstVertex;
                cmd.firstInstance = i;
                cmds.push_back(cmd);
            }
            frame.indirectBuffer->LoadData(cmds.data(), sizeof(GpuDrawCommand) * instanceCount);
            u32 cnt = instanceCount;
            frame.countBuffer->LoadData(&cnt, sizeof(u32));

            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline->GetHandle());
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline->GetLayout(), 0, 1, &frame.cullSet, 0, nullptr);

            struct CullPushConstants {
                Mat4 view;
                Mat4 proj;
                Vec4 screenTile;
                u32 lightCount;
                u32 maxPerTile;
                u32 tilesX;
                u32 tilesY;
                Vec4 zParams;
            } pc;

            pc.view = m_ViewMatrix;
            pc.proj = m_ProjectionMatrix;
            pc.proj[1][1] *= -1;
            pc.screenTile = Vec4((float)ext.width, (float)ext.height, (float)TILE_SIZE, (float)TILE_SIZE);
            pc.lightCount = (u32)m_PendingLights.size();
            pc.maxPerTile = MAX_LIGHTS_PER_TILE;
            pc.tilesX = (ext.width + TILE_SIZE - 1) / TILE_SIZE;
            pc.tilesY = (ext.height + TILE_SIZE - 1) / TILE_SIZE;
            pc.zParams = Vec4(0.1f, 10000.0f, 1.0f, 0.0f);

            vkCmdPushConstants(cb, m_CullPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cb, (pc.tilesX + 15) / 16, (pc.tilesY + 15) / 16, 1);

            VkBufferMemoryBarrier2 barriers[2]{};
            barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barriers[0].buffer = frame.tileHeaderBuffer->GetHandle();
            barriers[0].offset = 0;
            barriers[0].size = VK_WHOLE_SIZE;

            barriers[1] = barriers[0];
            barriers[1].buffer = frame.tileLightIndexBuffer->GetHandle();

            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = 2;
            dep.pBufferMemoryBarriers = barriers;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.image = m_OffscreenColor.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        VkClearValue cv{};
        cv.color = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};

        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.clearValue = cv;

        if (m_MsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
            colorAtt.imageView = m_MsaaColorImage.view;
            colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAtt.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            colorAtt.resolveImageView = m_OffscreenColor.view;
            colorAtt.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        } else {
            colorAtt.imageView = m_OffscreenColor.view;
            colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkRenderingAttachmentInfo depthAtt{};
        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAtt.imageView = m_DepthImage.view;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.clearValue.depthStencil = {1.0f, 0};


        UniformBufferObject ubo{};
        ubo.model = Mat4(1.0f);
        ubo.view = m_ViewMatrix;
        ubo.proj = m_ProjectionMatrix;
        ubo.proj[1][1] *= -1;
        ubo.camPos = Vec4(m_CameraPosition, 1.f);
        ubo.exposure = 1.0f;
        ubo.gamma = 2.2f;
        ubo.prefilteredCubeMipLevels = 1.0f;
        ubo.scaleIBLAmbient = 1.0f;
        ubo.lightCount = (int) m_PendingLights.size();
        ubo.padding0 = 0;
        ubo.padding1 = 1.0f;
        ubo.padding2 = 0.0f;
        ubo.screenDimensions = Vec2((float) ext.width, (float) ext.height);
        ubo.nearZ = 0.1f;
        ubo.farZ = 10000.0f;
        ubo.slicesZ = 1.0f;
        ubo._pad3 = 0.0f;
        ubo.reflectionVP = Mat4(1.0f);
        ubo.reflectionEnabled = 0;
        ubo.reflectionPass = 0;
        ubo._reflectPad0 = Vec2(0.0f);
        ubo.clipPlaneWS = Vec4(0.0f);
        ubo.reflectionIntensity = 1.0f;
        ubo.enableRayQueryReflections = 0;
        ubo.enableRayQueryTransparency = 0;
        ubo.geometryInfoCount = 0;
        ubo._rqReservedWorldPos = Vec4(0.0f);
        ubo.materialCount = (int) m_Materials.size();

        frame.uboBuffer->LoadData(&ubo, sizeof(ubo));

        if (!m_Materials.empty()) {
            m_MaterialBuffer->LoadData(m_Materials.data(), sizeof(MaterialData) * m_Materials.size());
        }

        {
            VkBufferMemoryBarrier2 b[5]{};
            b[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            b[0].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            b[0].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
            b[0].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b[0].buffer = m_MaterialBuffer->GetHandle();
            b[0].offset = 0;
            b[0].size = VK_WHOLE_SIZE;

            b[1] = b[0];
            b[1].buffer = m_TextureInfoBuffer->GetHandle();

            b[2] = b[0];
            b[2].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[2].buffer = frame.uboBuffer->GetHandle();

            b[3] = b[0];
            b[3].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b[3].buffer = frame.instanceBuffer->GetHandle();

            b[4] = b[0];
            b[4].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[4].buffer = frame.lightBuffer->GetHandle();

            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = 5;
            dep.pBufferMemoryBarriers = b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent = ext;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;
        ri.pDepthAttachment = &depthAtt;
        vkCmdBeginRendering(cb, &ri);

        VkViewport vp{0.f, 0.f, (float) ext.width, (float) ext.height, 0.f, 1.f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor{{0, 0}, ext};
        vkCmdSetScissor(cb, 0, 1, &scissor);
    }

    void Renderer::RenderQueue() {
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;

        if (m_CurrentFrameInstances.empty()) return;

        u32 instanceCount = (u32) m_CurrentFrameInstances.size();

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PbrPipeline->GetHandle());

        VkDescriptorSet sets[] = {frame.pbrSet, m_Textures.GetBindlessSet()};
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_PbrPipeline->GetLayout(), 0, 2, sets, 0, nullptr);

        vkCmdBindIndexBuffer(cb, m_Meshes.GetIndexBuffer()->GetHandle(), 0, VK_INDEX_TYPE_UINT32);

        VkBuffer vbufs[2] = {
            m_Meshes.GetVertexBuffer()->GetHandle(),
            frame.instanceBuffer->GetHandle()
        };
        VkDeviceSize offsets[2] = {0, 0};
        vkCmdBindVertexBuffers(cb, 0, 2, vbufs, offsets);

        vkCmdDrawIndexedIndirect(cb,
                                 frame.indirectBuffer->GetHandle(),
                                 0,
                                 instanceCount, sizeof(GpuDrawCommand));
    }

    void Renderer::EndRendering() {
        vkCmdEndRendering(m_Frames[m_CurrentFrame].commandBuffer);
    }

    void Renderer::EndFrameAndPresent() {
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;
        VkExtent2D ext = m_Swapchain->GetExtent();

        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = m_OffscreenColor.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = 0;
            b.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.image = m_Swapchain->GetImage(m_CurrentImageIndex);
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.imageView = m_Swapchain->GetImageView(m_CurrentImageIndex);
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent = ext;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;
        vkCmdBeginRendering(cb, &ri);

        VkViewport vp{0.f, 0.f, (float) ext.width, (float) ext.height, 0.f, 1.f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor{{0, 0}, ext};
        vkCmdSetScissor(cb, 0, 1, &scissor);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CompositePipeline->GetHandle());
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_CompositePipeline->GetLayout(), 0, 1,
                                &frame.compositeSet, 0, nullptr);

        CompositePushConstants cpc{};
        cpc.exposure = 1.0f;
        cpc.gamma = 2.2f;

        VkFormat format = m_Swapchain->GetImageFormat();
        if (format == VK_FORMAT_R8G8B8A8_SRGB || 
            format == VK_FORMAT_B8G8R8A8_SRGB || 
            format == VK_FORMAT_A8B8G8R8_SRGB_PACK32) {
            cpc.outputIsSRGB = 1;
        } else {
            cpc.outputIsSRGB = 0;
        }
        vkCmdPushConstants(cb, m_CompositePipeline->GetLayout(),
                           VK_SHADER_STAGE_ALL,
                           0, sizeof(CompositePushConstants), &cpc);

        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRendering(cb);

        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            b.dstAccessMask = 0;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            b.image = m_Swapchain->GetImage(m_CurrentImageIndex);
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        if (vkEndCommandBuffer(cb) != VK_SUCCESS)
            throw std::runtime_error("Failed to record command buffer");

        const u64 signalValue = ++m_TimelineValue;
        m_FrameBaseValue[m_CurrentFrame] = signalValue;

        VkCommandBufferSubmitInfo cmdInfo{};
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdInfo.commandBuffer = cb;

        VkSemaphoreSubmitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = m_ImageAvailableSemaphores[m_CurrentFrame];
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSemaphoreSubmitInfo signalInfos[2]{};
        signalInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfos[0].semaphore = m_TimelineSemaphore;
        signalInfos[0].value = signalValue;
        signalInfos[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfos[1].semaphore = m_PresentSemaphores[m_CurrentImageIndex];
        signalInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSubmitInfo2 submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &waitInfo;
        submit.signalSemaphoreInfoCount = 2;
        submit.pSignalSemaphoreInfos = signalInfos;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmdInfo;

        if (vkQueueSubmit2(m_Context.GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
            throw std::runtime_error("Failed to submit command buffer");

        if (m_Swapchain->Present(m_CurrentImageIndex,
                                 m_PresentSemaphores[m_CurrentImageIndex]))
            m_PendingResize = true;

        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    Scope<MaterialInstance> Renderer::CreateMaterialInstance(Ref<Material> material) {
        return CreateScope<MaterialInstance>(material);
    }

    void Renderer::DrawMesh(MeshHandle meshId, MaterialInstance &material, const Mat4 &model) {
        const auto *mesh = m_Meshes.Get(meshId);
        if (!mesh) return;

        GpuMeshInstance inst{};
        inst.modelMatrix = model;
        glm::mat3 n3 = glm::transpose(glm::inverse(glm::mat3(model)));
        for (int i = 0; i < 3; ++i) {
            inst.normalMatrix[i][0] = n3[i][0];
            inst.normalMatrix[i][1] = n3[i][1];
            inst.normalMatrix[i][2] = n3[i][2];
            inst.normalMatrix[i][3] = 0.f;
        }

        const MaterialData &md = material.GetData();

        u32 matIndex = 0;
        auto it = m_MaterialCache.find(md);
        if (it != m_MaterialCache.end()) {
            matIndex = it->second;
        } else {
            matIndex = (u32)m_Materials.size();
            m_Materials.push_back(md);
            m_MaterialCache[md] = matIndex;
            LOG_INFO("[Renderer] New Material added at index {} - texture: {}", matIndex, md.pbrBaseColorTexture);
        }

        inst.firstVertex = mesh->firstVertex;
        inst.firstIndex = mesh->firstIndex;
        inst.indexCount = mesh->indexCount;
        inst.materialIndex = matIndex;
        inst.flags = 0;
        m_CurrentFrameInstances.push_back(inst);
    }

    void Renderer::DrawModel(const Model &model, const Mat4 &transform) {
        for (const auto &sm: model.GetSubMeshes())
            DrawMesh(sm.meshId, *sm.material, transform);
    }

    void Renderer::CreateDescriptorLayouts() {
        {
            VkDescriptorSetLayoutBinding b[11]{};

            b[0].binding = 0;
            b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            b[0].descriptorCount = 1;
            b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            b[1].binding = 1;
            b[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            b[1].descriptorCount = 1;
            b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            b[2].binding = 6;
            b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[2].descriptorCount = 1;
            b[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            b[3].binding = 7;
            b[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[3].descriptorCount = 1;
            b[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            b[4].binding = 8;
            b[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[4].descriptorCount = 1;
            b[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            b[5].binding = 9;
            b[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[5].descriptorCount = 1;
            b[5].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            b[6].binding = 10;
            b[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            b[6].descriptorCount = 1;
            b[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            b[7].binding = 11;
            b[7].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            b[7].descriptorCount = 1;
            b[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            b[8].binding = 12;
            b[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[8].descriptorCount = 1;
            b[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            b[9].binding = 13;
            b[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[9].descriptorCount = 1;
            b[9].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            b[10].binding = 14;
            b[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[10].descriptorCount = 1;
            b[10].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorBindingFlags flags[11];
            for (int i = 0; i < 11; ++i) {
                flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
            }

            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
            bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlags.bindingCount = 11;
            bindingFlags.pBindingFlags = flags;

            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 11;
            ci.pBindings = b;
            ci.pNext = &bindingFlags;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr,
                                            &m_PbrSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create PBR descriptor set layout");
        }

        {
            VkDescriptorSetLayoutBinding b{};
            b.binding = 0;
            b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b.descriptorCount = 1;
            b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 1;
            ci.pBindings = &b;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr,
                                            &m_CompositeSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create composite descriptor set layout");
        }

        {
            VkDescriptorSetLayoutBinding b[4]{};
            b[0].binding = 0;
            b[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[0].descriptorCount = 1;
            b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            b[1].binding = 1;
            b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[1].descriptorCount = 1;
            b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            b[2].binding = 2;
            b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[2].descriptorCount = 1;
            b[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            b[3].binding = 3;
            b[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            b[3].descriptorCount = 1;
            b[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 4;
            ci.pBindings = b;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr,
                                            &m_CullSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create cull descriptor set layout");
        }
    }

    void Renderer::CreateDescriptorPool() {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, u32(MAX_FRAMES_IN_FLIGHT * 5)},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, u32(MAX_FRAMES_IN_FLIGHT * 20)},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, u32(MAX_FRAMES_IN_FLIGHT * 10)},
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, u32(MAX_FRAMES_IN_FLIGHT * 2)},
            {VK_DESCRIPTOR_TYPE_SAMPLER, u32(MAX_FRAMES_IN_FLIGHT * 5)},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, u32(MAX_FRAMES_IN_FLIGHT * 5)}
        };

        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.poolSizeCount = 6;
        ci.pPoolSizes = sizes;
        ci.maxSets = u32(MAX_FRAMES_IN_FLIGHT * 20);
        if (vkCreateDescriptorPool(m_Context.GetDevice(), &ci, nullptr,
                                   &m_DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create descriptor pool");
    }

    void Renderer::CreateGpuBuffers() {
        m_MaterialBuffer = CreateScope<Buffer>(
            m_Context, sizeof(MaterialData) * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_TextureInfoBuffer = CreateScope<Buffer>(
            m_Context, sizeof(shaderio::GltfTextureInfo) * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        // Initialize with default texture info
        std::vector<shaderio::GltfTextureInfo> tis(1024);
        for (int i = 0; i < 1024; ++i) {
            tis[i].uvTransform = Mat3x2(1.0f);
            tis[i].index = i - 1;
            tis[i].texCoord = 0;
        }
        m_TextureInfoBuffer->LoadData(tis.data(), sizeof(shaderio::GltfTextureInfo) * 1024);
    }

    void Renderer::UpdatePbrDescriptorSet(u32 fi) {
        FrameData &frame = m_Frames[fi];

        VkDescriptorBufferInfo uboI{};
        uboI.buffer = frame.uboBuffer->GetHandle();
        uboI.offset = 0;
        uboI.range = sizeof(UniformBufferObject);

        VkDescriptorBufferInfo lightI{};
        lightI.buffer = frame.lightBuffer->GetHandle();
        lightI.offset = 0;
        lightI.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo tileHdrI{};
        tileHdrI.buffer = frame.tileHeaderBuffer->GetHandle();
        tileHdrI.offset = 0;
        tileHdrI.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo tileLightI{};
        tileLightI.buffer = frame.tileLightIndexBuffer->GetHandle();
        tileLightI.offset = 0;
        tileLightI.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo instI{};
        instI.buffer = frame.instanceBuffer->GetHandle();
        instI.offset = 0;
        instI.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo matI{};
        matI.buffer = m_MaterialBuffer->GetHandle();
        matI.offset = 0;
        matI.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo texInfoI{};
        texInfoI.buffer = m_TextureInfoBuffer->GetHandle();
        texInfoI.offset = 0;
        texInfoI.range = VK_WHOLE_SIZE;

        // PBR set
        VkWriteDescriptorSet writes[9]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frame.pbrSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &uboI;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = frame.pbrSet;
        writes[1].dstBinding = 6;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightI;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = frame.pbrSet;
        writes[2].dstBinding = 7;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &tileHdrI;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = frame.pbrSet;
        writes[3].dstBinding = 8;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &tileLightI;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = frame.pbrSet;
        writes[4].dstBinding = 9;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &instI;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = frame.pbrSet;
        writes[5].dstBinding = 13;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo = &matI;

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = m_Textures.GetSampler();

        VkDescriptorImageInfo stubImg{};
        stubImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        stubImg.imageView = m_Textures.GetView(m_Textures.GetWhiteTextureId());

        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = frame.pbrSet;
        writes[6].dstBinding = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[6].descriptorCount = 1;
        writes[6].pImageInfo = &samplerInfo;

        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = frame.pbrSet;
        writes[7].dstBinding = 10;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[7].descriptorCount = 1;
        writes[7].pImageInfo = &stubImg;

        writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet = frame.pbrSet;
        writes[8].dstBinding = 14;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[8].descriptorCount = 1;
        writes[8].pBufferInfo = &texInfoI;

        vkUpdateDescriptorSets(m_Context.GetDevice(), 9, writes, 0, nullptr);

        // Cull set
        VkWriteDescriptorSet cullWrites[4]{};
        cullWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cullWrites[0].dstSet = frame.cullSet;
        cullWrites[0].dstBinding = 0;
        cullWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cullWrites[0].descriptorCount = 1;
        cullWrites[0].pBufferInfo = &lightI;

        cullWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cullWrites[1].dstSet = frame.cullSet;
        cullWrites[1].dstBinding = 1;
        cullWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cullWrites[1].descriptorCount = 1;
        cullWrites[1].pBufferInfo = &tileHdrI;

        cullWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cullWrites[2].dstSet = frame.cullSet;
        cullWrites[2].dstBinding = 2;
        cullWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cullWrites[2].descriptorCount = 1;
        cullWrites[2].pBufferInfo = &tileLightI;

        cullWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cullWrites[3].dstSet = frame.cullSet;
        cullWrites[3].dstBinding = 3;
        cullWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cullWrites[3].descriptorCount = 1;
        cullWrites[3].pBufferInfo = &uboI;

        vkUpdateDescriptorSets(m_Context.GetDevice(), 4, cullWrites, 0, nullptr);
    }

    void Renderer::UpdateCompositeDescriptorSet(u32 fi) {
        FrameData &frame = m_Frames[fi];

        VkDescriptorImageInfo imgI{};
        imgI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgI.imageView = m_OffscreenColor.view;
        imgI.sampler = m_OffscreenSampler;

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = frame.compositeSet;
        w.dstBinding = 0;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo = &imgI;

        vkUpdateDescriptorSets(m_Context.GetDevice(), 1, &w, 0, nullptr);
    }

    void Renderer::CreateCommandBuffers() {
        m_Frames.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandPoolCreateInfo poolCI{};
        poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolCI.queueFamilyIndex = m_Context.GetGraphicsQueueFamilyIndex();

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            FrameData &f = m_Frames[i];

            if (vkCreateCommandPool(m_Context.GetDevice(), &poolCI, nullptr,
                                    &f.commandPool) != VK_SUCCESS)
                throw std::runtime_error("Failed to create command pool");

            VkCommandBufferAllocateInfo cbAI{};
            cbAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cbAI.commandPool = f.commandPool;
            cbAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cbAI.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(m_Context.GetDevice(), &cbAI, &f.commandBuffer) != VK_SUCCESS)
                throw std::runtime_error("Failed to allocate command buffer");

            f.uboBuffer = CreateScope<Buffer>(
                m_Context, sizeof(UniformBufferObject),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.lightBuffer = CreateScope<Buffer>(
                m_Context, sizeof(LightData) * MAX_LIGHTS,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            struct TileHeader {
                u32 offset, count, pad0, pad1;
            };
            f.tileHeaderBuffer = CreateScope<Buffer>(
                m_Context, sizeof(TileHeader) * kMaxTiles,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.tileLightIndexBuffer = CreateScope<Buffer>(
                m_Context, sizeof(u32) * kMaxTiles * MAX_LIGHTS_PER_TILE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.instanceBuffer = CreateScope<Buffer>(
                m_Context, sizeof(GpuMeshInstance) * MAX_INSTANCES,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.indirectBuffer = CreateScope<Buffer>(
                m_Context, sizeof(GpuDrawCommand) * MAX_INSTANCES,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.countBuffer = CreateScope<Buffer>(
                m_Context, sizeof(u32),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

            VkDescriptorSetLayout layouts[3] = {m_PbrSetLayout, m_CompositeSetLayout, m_CullSetLayout};
            VkDescriptorSet sets[3];
            VkDescriptorSetAllocateInfo dsAI{};
            dsAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsAI.descriptorPool = m_DescriptorPool;
            dsAI.descriptorSetCount = 3;
            dsAI.pSetLayouts = layouts;
            if (vkAllocateDescriptorSets(m_Context.GetDevice(), &dsAI, sets) != VK_SUCCESS)
                throw std::runtime_error("Failed to allocate descriptor sets");
            f.pbrSet = sets[0];
            f.compositeSet = sets[1];
            f.cullSet = sets[2];

            UpdateCompositeDescriptorSet(i);
            UpdatePbrDescriptorSet(i);
        }
    }

    void Renderer::CreateSyncObjects() {
        VkSemaphoreCreateInfo binaryCI{};
        binaryCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        for (auto &s: m_ImageAvailableSemaphores)
            if (vkCreateSemaphore(m_Context.GetDevice(), &binaryCI, nullptr, &s) != VK_SUCCESS)
                throw std::runtime_error("Failed to create image-available semaphore");

        VkSemaphoreTypeCreateInfo tlCI{};
        tlCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        tlCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        tlCI.initialValue = 0;

        VkSemaphoreCreateInfo tlSI{};
        tlSI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        tlSI.pNext = &tlCI;
        if (vkCreateSemaphore(m_Context.GetDevice(), &tlSI, nullptr, &m_TimelineSemaphore) != VK_SUCCESS)
            throw std::runtime_error("Failed to create timeline semaphore");

        m_TimelineValue = 0;
        for (auto &v: m_FrameBaseValue) v = 0;

        m_PresentSemaphores.resize(m_Swapchain->GetImageCount());
        for (auto &s: m_PresentSemaphores)
            if (vkCreateSemaphore(m_Context.GetDevice(), &binaryCI, nullptr, &s) != VK_SUCCESS)
                throw std::runtime_error("Failed to create present semaphore");
    }

    void Renderer::BuildPbrPipeline() {
        std::vector<u8> vertSpv = ReadBinaryFile("assets/shaders/spv/pbr.vert.spv");
        std::vector<u8> fragSpv = ReadBinaryFile("assets/shaders/spv/pbr.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] PBR shaders not found");
            return; 
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.colorAttachmentFormat = m_OffscreenFormat;
        cfg.depthAttachmentFormat = m_DepthFormat;
        cfg.msaaSamples = m_MsaaSamples;
        cfg.pushConstantSize = sizeof(PbrPushConstants);
        cfg.descriptorSetLayouts = {m_PbrSetLayout, m_Textures.GetBindlessLayout()};

        cfg.vertexInputBindings.resize(2);
        cfg.vertexInputBindings[0] = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        cfg.vertexInputBindings[1] = {1, sizeof(GpuMeshInstance), VK_VERTEX_INPUT_RATE_INSTANCE};

        cfg.vertexInputAttributes.resize(11);
        cfg.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        cfg.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
        cfg.vertexInputAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
        cfg.vertexInputAttributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)};
        cfg.vertexInputAttributes[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuMeshInstance, modelMatrix)};
        cfg.vertexInputAttributes[5] = {
            5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuMeshInstance, modelMatrix) + 16
        };
        cfg.vertexInputAttributes[6] = {
            6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuMeshInstance, modelMatrix) + 32
        };
        cfg.vertexInputAttributes[7] = {
            7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuMeshInstance, modelMatrix) + 48
        };
        cfg.vertexInputAttributes[8] = {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuMeshInstance, normalMatrix)};
        cfg.vertexInputAttributes[9] = {
            9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuMeshInstance, normalMatrix) + 16
        };
        cfg.vertexInputAttributes[10] = {
            10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuMeshInstance, normalMatrix) + 32
        };
        cfg.vertexInputAttributes.resize(12);
        cfg.vertexInputAttributes[11] = {11, 1, VK_FORMAT_R32_UINT, offsetof(GpuMeshInstance, materialIndex)};

        m_PbrPipeline = CreateScope<Pipeline>(m_Context);
        m_PbrPipeline->BuildGraphics(vertSpv, fragSpv, cfg);

        VkDescriptorSetLayoutBinding stub{};
        stub.binding = 0;
        stub.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        stub.descriptorCount = 1;
        stub.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dci.bindingCount = 1;
        dci.pBindings = &stub;
        VkDescriptorSetLayout stubLayout;
        vkCreateDescriptorSetLayout(m_Context.GetDevice(), &dci, nullptr, &stubLayout);
        m_DefaultMaterial = CreateRef<Material>(m_Context, nullptr, stubLayout);
    }

    void Renderer::BuildCompositePipeline() {
        std::vector<u8> vertSpv = ReadBinaryFile("assets/shaders/spv/composite.vert.spv");
        std::vector<u8> fragSpv = ReadBinaryFile("assets/shaders/spv/composite.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] Composite shaders not found");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.colorAttachmentFormat = m_Swapchain->GetImageFormat();
        cfg.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        cfg.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        cfg.pushConstantSize = sizeof(CompositePushConstants);
        cfg.descriptorSetLayouts = {m_CompositeSetLayout};

        m_CompositePipeline = CreateScope<Pipeline>(m_Context);
        m_CompositePipeline->BuildGraphics(vertSpv, fragSpv, cfg);
    }

    void Renderer::BuildCullPipeline() {
        std::vector<u8> compSpv = ReadBinaryFile("assets/shaders/spv/forward_plus_cull.comp.spv");
        if (compSpv.empty()) {
            LOG_ERROR("[Renderer] Cull shader not found");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.computeEntryPoint = "main";
        cfg.pushConstantSize = 176; 
        cfg.descriptorSetLayouts = {m_CullSetLayout};

        m_CullPipeline = CreateScope<Pipeline>(m_Context);
        m_CullPipeline->BuildCompute(compSpv, cfg);
    }
} // namespace Manro
