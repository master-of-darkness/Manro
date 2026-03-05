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

namespace Engine {
    Renderer::Renderer() = default;

    Renderer::~Renderer() {
        Shutdown();
    }

    void Renderer::Initialize(IWindow *window, u32 width, u32 height) {
        m_Context.Initialize("GameEngine");
        m_Context.CreateSurface(window);

        m_Swapchain = CreateScope<Swapchain>(m_Context);
        m_Swapchain->Initialize(width, height);

        m_MsaaSamples = m_Context.GetMaxUsableSampleCount();
        LOG_INFO("[Renderer] MSAA samples: {}", (int)m_MsaaSamples);

        CreateColorResources(width, height);
        CreateDepthResources(width, height);
        CreateCommandBuffers();
        CreateSyncObjects();
        CreateDescriptorPool();
        CreateDescriptorSetLayout();
        CreateDefaultSampler();
        LoadShadersAndPipeline();
        CreateWhiteTexture();

        m_ActiveDescriptorSet = m_Textures[m_WhiteTextureId].descriptorSet;

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

        DestroyColorResources();
        CreateColorResources(w, h);

        DestroyDepthResources();
        CreateDepthResources(w, h);

        LOG_INFO("[Renderer] Swapchain recreated {}x{}", w, h);
    }

    void Renderer::CreateDepthResources(u32 width, u32 height) {
        VkImageCreateInfo depthInfo{};
        depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthInfo.imageType = VK_IMAGE_TYPE_2D;
        depthInfo.extent = {width, height, 1};
        depthInfo.mipLevels = 1;
        depthInfo.arrayLayers = 1;
        depthInfo.format = m_DepthFormat;
        depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo depthAllocInfo{};
        depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        depthAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateImage(m_Context.GetAllocator(), &depthInfo, &depthAllocInfo,
                           &m_DepthImage, &m_DepthAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] Failed to create depth image!");
        }

        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = m_DepthImage;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = m_DepthFormat;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthViewInfo.subresourceRange.baseMipLevel = 0;
        depthViewInfo.subresourceRange.levelCount = 1;
        depthViewInfo.subresourceRange.baseArrayLayer = 0;
        depthViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Context.GetDevice(), &depthViewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] Failed to create depth image view!");
        }
    }

    void Renderer::DestroyDepthResources() {
        if (m_DepthImageView) {
            vkDestroyImageView(m_Context.GetDevice(), m_DepthImageView, nullptr);
            m_DepthImageView = VK_NULL_HANDLE;
        }
        if (m_DepthImage) {
            vmaDestroyImage(m_Context.GetAllocator(), m_DepthImage, m_DepthAllocation);
            m_DepthImage = VK_NULL_HANDLE;
            m_DepthAllocation = nullptr;
        }
    }

    void Renderer::CreateColorResources(u32 width, u32 height) {
        VkImageCreateInfo colorInfo{};
        colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorInfo.imageType = VK_IMAGE_TYPE_2D;
        colorInfo.extent.width = width;
        colorInfo.extent.height = height;
        colorInfo.extent.depth = 1;
        colorInfo.mipLevels = 1;
        colorInfo.arrayLayers = 1;
        colorInfo.format = m_Swapchain->GetImageFormat();
        colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        colorInfo.samples = m_MsaaSamples;
        colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo colorAllocInfo{};
        colorAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        colorAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateImage(m_Context.GetAllocator(), &colorInfo, &colorAllocInfo,
                           &m_ColorImage, &m_ColorAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] Failed to create multisampled color image!");
        }

        VkImageViewCreateInfo colorViewInfo{};
        colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorViewInfo.image = m_ColorImage;
        colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorViewInfo.format = m_Swapchain->GetImageFormat();
        colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorViewInfo.subresourceRange.baseMipLevel = 0;
        colorViewInfo.subresourceRange.levelCount = 1;
        colorViewInfo.subresourceRange.baseArrayLayer = 0;
        colorViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Context.GetDevice(), &colorViewInfo, nullptr, &m_ColorImageView) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] Failed to create multisampled color image view!");
        }
    }

    void Renderer::DestroyColorResources() {
        if (m_ColorImageView) {
            vkDestroyImageView(m_Context.GetDevice(), m_ColorImageView, nullptr);
            m_ColorImageView = VK_NULL_HANDLE;
        }
        if (m_ColorImage) {
            vmaDestroyImage(m_Context.GetAllocator(), m_ColorImage, m_ColorAllocation);
            m_ColorImage = VK_NULL_HANDLE;
            m_ColorAllocation = nullptr;
        }
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

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
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
        colorAttachment.imageView = m_Swapchain->GetImageView(m_CurrentImageIndex);
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue = cv;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = m_DepthImageView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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

        m_ActiveDescriptorSet = m_Textures[m_WhiteTextureId].descriptorSet;
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

    void Renderer::Shutdown() {
        if (!m_Context.GetDevice()) return;
        vkDeviceWaitIdle(m_Context.GetDevice());

        for (auto &[id, tex]: m_Textures) {
            if (tex.view) vkDestroyImageView(m_Context.GetDevice(), tex.view, nullptr);
            if (tex.image) vmaDestroyImage(m_Context.GetAllocator(), tex.image, tex.allocation);
        }
        m_Textures.clear();

        DestroyColorResources();
        DestroyDepthResources();

        if (m_Sampler) vkDestroySampler(m_Context.GetDevice(), m_Sampler, nullptr);
        if (m_DescriptorSetLayout) vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_DescriptorSetLayout, nullptr);
        if (m_DescriptorPool) vkDestroyDescriptorPool(m_Context.GetDevice(), m_DescriptorPool, nullptr);

        m_Pipeline.reset();
        m_ShaderCompiler.reset();
        m_Meshes.clear();
        m_VertexBuffer.reset();
        m_IndexBuffer.reset();

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
        m_Context.Shutdown();
    }

    void Renderer::CreateDescriptorPool() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 256;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 256;

        if (vkCreateDescriptorPool(m_Context.GetDevice(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("[Renderer] Failed to create descriptor pool!");
    }

    void Renderer::CreateDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &m_DescriptorSetLayout) !=
            VK_SUCCESS)
            throw std::runtime_error("[Renderer] Failed to create descriptor set layout!");
    }

    void Renderer::CreateDefaultSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

        if (vkCreateSampler(m_Context.GetDevice(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
            throw std::runtime_error("[Renderer] Failed to create texture sampler!");
    }

    void Renderer::CreateWhiteTexture() {
        const u8 whitePixel[4] = {255, 255, 255, 255};
        m_WhiteTextureId = UploadTextureInternal(whitePixel, 1, 1);
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
        config.depthAttachmentFormat = m_DepthFormat;
        config.descriptorSetLayout = m_DescriptorSetLayout;

        config.vertexInputBindings.resize(1);
        config.vertexInputBindings[0].binding = 0;
        config.vertexInputBindings[0].stride = sizeof(Vertex);
        config.vertexInputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        config.vertexInputAttributes.resize(3);
        config.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        config.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};
        config.vertexInputAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};

        config.pushConstantSize = sizeof(MeshPushConstants);

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

    u32 Renderer::UploadTextureInternal(const u8 *pixels, int width, int height) {
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

        VkBufferCreateInfo stagingBufInfo{};
        stagingBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufInfo.size = imageSize;
        stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer stagingBuf{};
        VmaAllocation stagingAlloc{};
        VmaAllocationInfo stagingAllocRes{};

        if (vmaCreateBuffer(m_Context.GetAllocator(), &stagingBufInfo, &stagingAllocInfo,
                            &stagingBuf, &stagingAlloc, &stagingAllocRes) != VK_SUCCESS) {
            LOG_ERROR("[Renderer] Failed to create texture staging buffer!");
            return 0;
        }
        std::memcpy(stagingAllocRes.pMappedData, pixels, imageSize);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<u32>(width), static_cast<u32>(height), 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo gpuAllocInfo{};
        gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        LoadedTexture tex{};
        if (vmaCreateImage(m_Context.GetAllocator(), &imageInfo, &gpuAllocInfo,
                           &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
            LOG_ERROR("[Renderer] Failed to create VkImage!");
            vmaDestroyBuffer(m_Context.GetAllocator(), stagingBuf, stagingAlloc);
            return 0;
        }

        VkCommandPool tmpPool{};
        VkCommandBuffer tmpCmd{};

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_Context.GetGraphicsQueueFamilyIndex();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkCreateCommandPool(m_Context.GetDevice(), &poolInfo, nullptr, &tmpPool);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = tmpPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_Context.GetDevice(), &allocInfo, &tmpCmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(tmpCmd, &beginInfo);

        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(tmpCmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<u32>(width), static_cast<u32>(height), 1};
        vkCmdCopyBufferToImage(tmpCmd, stagingBuf, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(tmpCmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        vkEndCommandBuffer(tmpCmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &tmpCmd;
        vkQueueSubmit(m_Context.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context.GetGraphicsQueue());

        vkDestroyCommandPool(m_Context.GetDevice(), tmpPool, nullptr);
        vmaDestroyBuffer(m_Context.GetAllocator(), stagingBuf, stagingAlloc);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = tex.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        if (vkCreateImageView(m_Context.GetDevice(), &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
            LOG_ERROR("[Renderer] Failed to create texture image view!");
            return 0;
        }

        VkDescriptorSetAllocateInfo dsAllocInfo{};
        dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAllocInfo.descriptorPool = m_DescriptorPool;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &m_DescriptorSetLayout;
        vkAllocateDescriptorSets(m_Context.GetDevice(), &dsAllocInfo, &tex.descriptorSet);

        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfo.imageView = tex.view;
        imgInfo.sampler = m_Sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = tex.descriptorSet;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imgInfo;
        vkUpdateDescriptorSets(m_Context.GetDevice(), 1, &write, 0, nullptr);

        u32 id = m_NextTextureId++;
        m_Textures.emplace(id, std::move(tex));
        return id;
    }

    u32 Renderer::UploadTexture(const TextureData &data) {
        if (data.pixels.empty() || data.width <= 0 || data.height <= 0) return 0;
        return UploadTextureInternal(data.pixels.data(), data.width, data.height);
    }

    void Renderer::BindTexture(u32 textureId) {
        if (textureId == 0) {
            m_ActiveDescriptorSet = m_Textures[m_WhiteTextureId].descriptorSet;
            return;
        }
        auto it = m_Textures.find(textureId);
        m_ActiveDescriptorSet = (it != m_Textures.end())
                                    ? it->second.descriptorSet
                                    : m_Textures[m_WhiteTextureId].descriptorSet;
    }

    u32 Renderer::UploadMesh(const ModelData &data) {
        if (data.vertices.empty() || data.indices.empty()) return 0;

        static_assert(sizeof(ModelVertex) == sizeof(Vertex),
                      "ModelVertex and Renderer::Vertex layout mismatch!");

        LoadedMesh mesh;
        mesh.indexCount = static_cast<u32>(data.indices.size());

        mesh.vertexBuffer = CreateScope<Buffer>(m_Context,
                                                sizeof(ModelVertex) * data.vertices.size(),
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        mesh.vertexBuffer->LoadData(data.vertices.data(), sizeof(ModelVertex) * data.vertices.size());

        mesh.indexBuffer = CreateScope<Buffer>(m_Context,
                                               sizeof(u32) * data.indices.size(),
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        mesh.indexBuffer->LoadData(data.indices.data(), sizeof(u32) * data.indices.size());

        u32 id = m_NextMeshId++;
        m_Meshes.emplace(id, std::move(mesh));
        return id;
    }

    void Renderer::DrawMesh(u32 meshId, const Mat4 &mvp) {
        auto it = m_Meshes.find(meshId);
        if (it == m_Meshes.end()) return;
        const LoadedMesh &mesh = it->second;

        VkCommandBuffer cb = m_Frames[m_CurrentFrame].commandBuffer;

        MeshPushConstants pc;
        pc.mvp = mvp;
        pc.tint = m_TintColor;

        vkCmdPushConstants(cb, m_Pipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &pc);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_Pipeline->GetLayout(), 0, 1, &m_ActiveDescriptorSet, 0, nullptr);

        VkBuffer vb[] = {mesh.vertexBuffer->GetHandle()};
        VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vb, offs);
        vkCmdBindIndexBuffer(cb, mesh.indexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, mesh.indexCount, 1, 0, 0, 0);
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

        VkCommandBuffer cb = m_Frames[m_CurrentFrame].commandBuffer;

        vkCmdPushConstants(cb, m_Pipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &mvp);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_Pipeline->GetLayout(), 0, 1,
                                &m_Textures[m_WhiteTextureId].descriptorSet, 0, nullptr);

        VkBuffer vb[] = {m_VertexBuffer->GetHandle()};
        VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vb, offs);
        vkCmdBindIndexBuffer(cb, m_IndexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, 36, 1, 0, 0, 0);
    }
} // namespace Engine
