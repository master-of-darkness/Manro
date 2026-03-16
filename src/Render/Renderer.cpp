#include <Manro/Render/Renderer.h>
#include <Manro/Render/Vulkan/VulkanHelpers.h>
#include <Manro/Core/Logger.h>
#include <stdexcept>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>

namespace Manro {

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
        LOG_INFO("[Renderer] MSAA samples: {} (requested: {}, max: {})",
                 (int)m_MsaaSamples, (int)msaaSamples, (int)maxSamples);

        m_PendingWidth = width;
        m_PendingHeight = height;

        CreateColorResources(width, height);
        CreateDepthResources(width, height);
        CreateDescriptorPool();
        CreateGpuBuffers();
        LoadShadersAndPipeline();
        CreateCommandBuffers();
        CreateSyncObjects();

        m_Materials.push_back(MaterialData{});
    }

    Renderer::~Renderer() {
        if (!m_Context.GetDevice()) return;
        vkDeviceWaitIdle(m_Context.GetDevice());

        m_DefaultMaterial.reset();
        m_CullPipeline.reset();
        m_IndirectPipeline.reset();

        DestroyImage(m_Context, m_ColorImage);
        DestroyImage(m_Context, m_DepthImage);

        for (auto &frame: m_Frames) {
            if (frame.inFlightFence)
                vkDestroyFence(m_Context.GetDevice(), frame.inFlightFence, nullptr);
            if (frame.commandPool)
                vkDestroyCommandPool(m_Context.GetDevice(), frame.commandPool, nullptr);
        }

        for (auto semaphore : m_ImageAvailableSemaphores)
            vkDestroySemaphore(m_Context.GetDevice(), semaphore, nullptr);
        for (auto semaphore : m_RenderFinishedSemaphores)
            vkDestroySemaphore(m_Context.GetDevice(), semaphore, nullptr);

        if (m_DescriptorPool) vkDestroyDescriptorPool(m_Context.GetDevice(), m_DescriptorPool, nullptr);
        if (m_GlobalSetLayout) vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_GlobalSetLayout, nullptr);

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

        // Update render finished semaphores if image count changed
        if (m_RenderFinishedSemaphores.size() != m_Swapchain->GetImageCount()) {
            for (auto semaphore : m_RenderFinishedSemaphores)
                vkDestroySemaphore(m_Context.GetDevice(), semaphore, nullptr);

            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            m_RenderFinishedSemaphores.resize(m_Swapchain->GetImageCount());
            for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++) {
                if (vkCreateSemaphore(m_Context.GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to create render finished semaphore during swapchain recreation!");
                }
            }
        }

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

        m_CurrentImageIndex = m_Swapchain->AcquireNextImage(m_ImageAvailableSemaphores[m_CurrentFrame]);
        if (m_CurrentImageIndex == UINT32_MAX) {
            return false;
        }

        vkResetFences(m_Context.GetDevice(), 1, &frame.inFlightFence);
        vkResetCommandBuffer(frame.commandBuffer, 0);

        m_CurrentFrameInstances.clear();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin recording command buffer!");

        return true;
    }

    void Renderer::BeginRendering(Vec4 clearColor) {
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;

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
        vkCmdPipelineBarrier2(cb, &dep);

        VkClearValue cv{};
        cv.color = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.clearValue = cv;

        if (m_MsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
            colorAttachment.imageView = m_ColorImage.view;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            colorAttachment.resolveImageView = m_Swapchain->GetImageView(m_CurrentImageIndex);
            colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        } else {
            colorAttachment.imageView = m_Swapchain->GetImageView(m_CurrentImageIndex);
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = m_DepthImage.view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea.extent = m_Swapchain->GetExtent();
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
        renderInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cb, &renderInfo);
 
        VkViewport viewport{0.0f, 0.0f, (float)m_Swapchain->GetExtent().width, (float)m_Swapchain->GetExtent().height, 0.0f, 1.0f};
        vkCmdSetViewport(cb, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, m_Swapchain->GetExtent()};
        vkCmdSetScissor(cb, 0, 1, &scissor);
 
        // Update UBO
        UniformBufferObject ubo{};
        ubo.view = m_ViewMatrix;
        ubo.proj = m_ProjectionMatrix;
        ubo.proj[1][1] *= -1;
        ubo.camPos = Vec4(0, 0, 0, 1); 
        ubo.screenDimensions = Vec2(viewport.width, viewport.height);
        frame.uboBuffer->LoadData(&ubo, sizeof(ubo));
    }
 
    void Renderer::RenderQueue() {
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;

        if (m_CurrentFrameInstances.empty()) return;

        u32 instanceCount = static_cast<u32>(m_CurrentFrameInstances.size());
        frame.instanceBuffer->LoadData(m_CurrentFrameInstances.data(), sizeof(GpuMeshInstance) * instanceCount);

        std::vector<GpuDrawCommand> cmds;
        for(u32 i=0; i<instanceCount; ++i) {
            GpuDrawCommand cmd{};
            cmd.indexCount = m_CurrentFrameInstances[i].indexCount;
            cmd.instanceCount = 1;
            cmd.firstIndex = m_CurrentFrameInstances[i].firstIndex;
            cmd.vertexOffset = (int)m_CurrentFrameInstances[i].firstVertex;
            cmd.firstInstance = i;
            cmds.push_back(cmd);
        }
        frame.indirectBuffer->LoadData(cmds.data(), sizeof(GpuDrawCommand) * instanceCount);
        u32 count = instanceCount;
        frame.countBuffer->LoadData(&count, 4);

        UpdateGlobalDescriptorSet(cb);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_IndirectPipeline->GetHandle());
        
        VkDescriptorSet sets[] = { frame.globalSet, m_Textures.GetBindlessSet() };
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_IndirectPipeline->GetLayout(), 0, 2, sets, 0, nullptr);
        
        vkCmdBindIndexBuffer(cb, m_Meshes.GetIndexBuffer()->GetHandle(), 0, VK_INDEX_TYPE_UINT32);

        // Also bind the instance buffer as a vertex buffer if the shader uses it via attributes
        VkDeviceSize offsets[2] = { 0, 0 };
        VkBuffer vertexBuffers[2] = { m_Meshes.GetVertexBuffer()->GetHandle(), frame.instanceBuffer->GetHandle() };
        vkCmdBindVertexBuffers(cb, 0, 2, vertexBuffers, offsets);

        vkCmdDrawIndexedIndirectCount(cb, frame.indirectBuffer->GetHandle(), 0, frame.countBuffer->GetHandle(), 0, MAX_INSTANCES, sizeof(GpuDrawCommand));
    }

    void Renderer::EndRendering() {
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
        waitInfo.semaphore = m_ImageAvailableSemaphores[m_CurrentFrame];
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSemaphoreSubmitInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = m_RenderFinishedSemaphores[m_CurrentImageIndex];
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

        bool needsRecreate = m_Swapchain->Present(m_CurrentImageIndex, m_RenderFinishedSemaphores[m_CurrentImageIndex]);
        if (needsRecreate)
            m_PendingResize = true;
 
        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::BindTexture(TextureHandle id) {
        if (!m_DefaultMaterialInstance) {
            m_DefaultMaterialInstance = CreateMaterialInstance(m_DefaultMaterial);
        }
        m_DefaultMaterialInstance->SetTexture(id);
    }

    Scope<MaterialInstance> Renderer::CreateMaterialInstance(Ref<Material> material) {
        auto inst = CreateScope<MaterialInstance>(material);
        inst->CreateDescriptorSets(m_DescriptorPool, MAX_FRAMES_IN_FLIGHT);
        return inst;
    }
 
    void Renderer::DrawMesh(MeshHandle meshId, const Mat4 &model) {
        if (!m_DefaultMaterialInstance) {
            m_DefaultMaterialInstance = CreateScope<MaterialInstance>(m_DefaultMaterial);
            m_DefaultMaterialInstance->CreateDescriptorSets(m_DescriptorPool, MAX_FRAMES_IN_FLIGHT);
        }
        DrawMesh(meshId, *m_DefaultMaterialInstance, model);
    }
 
    void Renderer::DrawMesh(MeshHandle meshId, MaterialInstance &material, const Mat4 &model) {
        const auto *mesh = m_Meshes.Get(meshId);
        if (!mesh) return;

        GpuMeshInstance inst{};
        inst.modelMatrix = model;
        
        glm::mat3 model3 = glm::mat3(model);
        glm::mat3 normalMat = glm::transpose(glm::inverse(model3));
        for(int i=0; i<3; ++i) {
            for(int j=0; j<3; ++j) {
                inst.normalMatrix[i][j] = normalMat[i][j];
            }
            inst.normalMatrix[i][3] = 0.0f;
        }

        u32 matIndex = 0;
        MaterialData matData = material.GetData();
        bool found = false;
        
        for(u32 i=0; i < (u32)m_Materials.size(); ++i) {
            if (std::memcmp(&m_Materials[i], &matData, sizeof(MaterialData)) == 0) {
                matIndex = i;
                found = true;
                break;
            }
        }
        
        if (!found) {
            if (m_Materials.size() < 1024) {
                matIndex = (u32)m_Materials.size();
                m_Materials.push_back(matData);
                m_MaterialBuffer->LoadData(m_Materials.data(), sizeof(MaterialData) * m_Materials.size());
            } else {
                LOG_WARN("[Renderer] Material buffer overflow (max 1024)!");
            }
        }

        inst.firstVertex = mesh->firstVertex;
        inst.firstIndex = mesh->firstIndex;
        inst.indexCount = mesh->indexCount;
        inst.materialIndex = matIndex;
        inst.flags = 0;

        m_CurrentFrameInstances.push_back(inst);
    }

    void Renderer::CreateGpuBuffers() {
        m_MaterialBuffer = CreateScope<Buffer>(m_Context, sizeof(MaterialData) * 1024,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
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

            m_Frames[i].uboBuffer = CreateScope<Buffer>(m_Context, sizeof(UniformBufferObject),
                                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            m_Frames[i].instanceBuffer = CreateScope<Buffer>(m_Context, sizeof(GpuMeshInstance) * MAX_INSTANCES,
                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                             VMA_MEMORY_USAGE_CPU_TO_GPU);

            m_Frames[i].indirectBuffer = CreateScope<Buffer>(m_Context, sizeof(GpuDrawCommand) * MAX_INSTANCES,
                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                                             VMA_MEMORY_USAGE_CPU_TO_GPU);

            m_Frames[i].countBuffer = CreateScope<Buffer>(m_Context, sizeof(u32),
                                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                                          VMA_MEMORY_USAGE_CPU_TO_GPU);

            m_Frames[i].statsBuffer = CreateScope<Buffer>(m_Context, 16,
                                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            VkDescriptorSetAllocateInfo dsAllocInfo{};
            dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsAllocInfo.descriptorPool = m_DescriptorPool;
            dsAllocInfo.descriptorSetCount = 1;
            dsAllocInfo.pSetLayouts = &m_GlobalSetLayout;

            if (vkAllocateDescriptorSets(m_Context.GetDevice(), &dsAllocInfo, &m_Frames[i].globalSet) != VK_SUCCESS)
                throw std::runtime_error("Failed to allocate global descriptor sets!");
        }
    }

    void Renderer::CreateSyncObjects() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(m_Context.GetDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(m_Context.GetDevice(), &fenceInfo, nullptr, &m_Frames[i].inFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create synchronization objects for a frame!");
            }
        }

        m_RenderFinishedSemaphores.resize(m_Swapchain->GetImageCount());
        for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++) {
            if (vkCreateSemaphore(m_Context.GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create render finished semaphore for a swapchain image!");
            }
        }
    }

    void Renderer::CreateDescriptorPool() {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100}
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = 10;

        if (vkCreateDescriptorPool(m_Context.GetDevice(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create descriptor pool!");
    }

    void Renderer::UpdateGlobalDescriptorSet(VkCommandBuffer cb) {
        FrameData &frame = m_Frames[m_CurrentFrame];

        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = frame.uboBuffer->GetHandle();
        uboInfo.offset = 0;
        uboInfo.range = sizeof(UniformBufferObject);

        VkDescriptorBufferInfo matInfo{};
        matInfo.buffer = m_MaterialBuffer->GetHandle();
        matInfo.offset = 0;
        matInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frame.globalSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &uboInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = frame.globalSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &matInfo;

        vkUpdateDescriptorSets(m_Context.GetDevice(), 2, writes, 0, nullptr);
    }
 
    void Renderer::LoadShadersAndPipeline() {
        std::vector<u8> vertSpv = ReadBinaryFile("assets/shaders/spv/texturedMesh.vert.spv");
        std::vector<u8> fragSpv = ReadBinaryFile("assets/shaders/spv/texturedMesh.frag.spv");
 
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] Failed to load precompiled shaders!");
            return;
        }
 
        // Create Global Descriptor Set Layout
        VkDescriptorSetLayoutBinding globalBindings[2]{};
        globalBindings[0].binding = 0; // UBO
        globalBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        globalBindings[0].descriptorCount = 1;
        globalBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        globalBindings[1].binding = 1; // Material Buffer
        globalBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        globalBindings[1].descriptorCount = 1;
        globalBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo globalLayoutInfo{};
        globalLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        globalLayoutInfo.bindingCount = 2;
        globalLayoutInfo.pBindings = globalBindings;

        if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &globalLayoutInfo, nullptr, &m_GlobalSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create global descriptor set layout!");

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
 
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
 
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
 
        VkDescriptorSetLayout descriptorSetLayout;
        if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create material descriptor set layout!");
 
        auto pipeline = CreateScope<Pipeline>(m_Context);
        PipelineConfigParams config{};
        config.vertexEntryPoint = "main";
        config.fragmentEntryPoint = "main";
        config.colorAttachmentFormat = m_Swapchain->GetImageFormat();
        config.depthAttachmentFormat = m_DepthFormat;
        config.descriptorSetLayouts = { m_GlobalSetLayout, m_Textures.GetBindlessLayout() };
 
        config.vertexInputBindings.resize(2);
        config.vertexInputBindings[0].binding = 0;
        config.vertexInputBindings[0].stride = sizeof(Vertex);
        config.vertexInputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        config.vertexInputBindings[1].binding = 1;
        config.vertexInputBindings[1].stride = sizeof(GpuMeshInstance); 
        config.vertexInputBindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        config.vertexInputAttributes.resize(11);
        config.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        config.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
        config.vertexInputAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
        config.vertexInputAttributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)};

        // Instance model matrix (locations 4, 5, 6, 7)
        config.vertexInputAttributes[4] = { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 };
        config.vertexInputAttributes[5] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 };
        config.vertexInputAttributes[6] = { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 };
        config.vertexInputAttributes[7] = { 7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 };

        // Instance normal matrix (locations 8, 9, 10)
        config.vertexInputAttributes[8] = { 8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 64 };
        config.vertexInputAttributes[9] = { 9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 80 };
        config.vertexInputAttributes[10] = { 10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 96 };

        // Material index (location 11)
        config.vertexInputAttributes.resize(12);
        config.vertexInputAttributes[11] = { 11, 1, VK_FORMAT_R32_UINT, 112 };

 
        config.msaaSamples = m_MsaaSamples;
 
        pipeline->BuildGraphics(vertSpv, fragSpv, config);
        m_IndirectPipeline = std::move(pipeline);
        m_DefaultMaterial = CreateRef<Material>(m_Context, nullptr, descriptorSetLayout); // Material holds the legacy layout for now
    }
} // namespace Manro
