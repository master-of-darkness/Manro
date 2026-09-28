#include "VoxelRenderer.h"
#include "VoxelWorld.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Pipeline.h"
#include "../Vulkan/PipelineCache.h"
#include "../Vulkan/Buffer.h"

#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Manro {
    CVoxelRenderer::CVoxelRenderer(CVulkanContext &ctx, CVirtualFS &vfs)
        : m_Context(ctx), m_Vfs(vfs) {
        m_World = CreateScope<CVoxelWorld>(ctx);
    }

    CVoxelRenderer::~CVoxelRenderer() { Shutdown(); }

    void CVoxelRenderer::Init(CPipelineCache &cache, u32 width, u32 height) {
        VoxelWorldDesc_t desc{};
        desc.virtualDim = 64;
        desc.voxelSize = 1.f;
        desc.maxResidentBricks = 8192;
        m_World->Init(desc);

        BuildPipelines(cache);
        CreateHiZ(width, height);

        m_VisibilityBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 2 * desc.maxResidentBricks,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
        // VkDispatchIndirectCommand layout for pass-2 re-dispatch.
        m_TaskIndirectBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(VkDispatchIndirectCommand),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_TaskCountBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(u32),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
        m_PaletteBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(Vec4) * 512,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_SunBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(Vec4) * 2,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        const u32 cascadeRes = 64;
        m_CascadeBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(Vec4) * cascadeRes * cascadeRes * cascadeRes * 3,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
        m_EditStaging = CreateScope<CBuffer>(
            m_Context, sizeof(VoxelEditCmd_t) * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_FrameParamsRing[0] = CreateScope<CBuffer>(
            m_Context, sizeof(VoxelFrameParams_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        for (u32 i = 1; i < CVoxelRenderer::kFlightSlots; ++i) {
            m_FrameParamsRing[i] = CreateScope<CBuffer>(
                m_Context, sizeof(VoxelFrameParams_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);
        }
        // DEBUG counters: uint[8], zeroed per frame on CPU, atomically
        // incremented by task/mesh. Host-visible for printf diagnosis.
        m_DebugReadback = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 8,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        Vec4 sun[2] = {Vec4(0.3f, -1.f, 0.2f, 0.f), Vec4(1.f, 0.98f, 0.9f, 3.f)};
        m_SunBuffer->LoadData(sun, sizeof(sun));

        // Distinct palette entries so the checkerboard is visible: 0 = warm grey,
        // 1 = teal. Remaining entries default mid-grey.
        {
            std::vector<Vec4> palette(512, Vec4(0.6f, 0.6f, 0.6f, 1.f));
            palette[0] = Vec4(0.85f, 0.25f, 0.15f, 1.f);
            palette[1] = Vec4(0.1f, 0.7f, 0.65f, 1.f);
            m_PaletteBuffer->LoadData(palette.data(), sizeof(Vec4) * palette.size());
        }

        m_bInitialized = true;
        LOG_INFO("[CVoxelRenderer] Initialized (sparse bricks, task/mesh PSO, HiZ {}x{})", width, height);
    }

    void CVoxelRenderer::Shutdown() {
        VkDevice device = m_Context.GetDevice();
        if (device) {
            vkDeviceWaitIdle(device);
        }
        DestroyHiZ();
        m_TaskMeshPipeline.reset();
        m_SpdPipeline.reset();
        m_EditPipeline.reset();
        m_GiInjectPipeline.reset();
        m_GiPropagatePipeline.reset();
        m_VisibilityBuffer.reset();
        m_TaskIndirectBuffer.reset();
        m_TaskCountBuffer.reset();
        m_PaletteBuffer.reset();
        m_SunBuffer.reset();
        m_CascadeBuffer.reset();
        m_EditStaging.reset();
        for (auto &slot : m_FrameParamsRing)
            slot.reset();
        m_DebugReadback.reset();
        if (m_World)
            m_World->Shutdown();
        m_bInitialized = false;
    }

    void CVoxelRenderer::BuildPipelines(CPipelineCache &cache) {
        auto taskSpv = m_Vfs.ReadFile("shaders://voxel_task.task.spv");
        auto meshSpv = m_Vfs.ReadFile("shaders://voxel_mesh.mesh.spv");
        auto fragSpv = m_Vfs.ReadFile("shaders://voxel_shade.frag.spv");
        auto spdSpv = m_Vfs.ReadFile("shaders://voxel_hiz_spd.comp.spv");
        auto editSpv = m_Vfs.ReadFile("shaders://voxel_edit.comp.spv");
        auto giInjectSpv = m_Vfs.ReadFile("shaders://voxel_gi_inject.comp.spv");
        auto giPropSpv = m_Vfs.ReadFile("shaders://voxel_gi_propagate.comp.spv");

        if (taskSpv.empty() || meshSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel task/mesh shaders missing");
        if (fragSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel shade fragment missing");
        if (spdSpv.empty() || editSpv.empty() || giInjectSpv.empty() || giPropSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel compute shaders missing");

        PipelineConfigParams_t meshCfg{};
        meshCfg.vertexEntryPoint = "main";
        meshCfg.fragmentEntryPoint = "main";
        meshCfg.computeEntryPoint = "main";
        meshCfg.colorAttachmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        meshCfg.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        meshCfg.msaaSamples = VK_SAMPLE_COUNT_1_BIT; // voxel path is MSAA-free
        meshCfg.pushConstantStages = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT |
                                     VK_SHADER_STAGE_FRAGMENT_BIT;
        meshCfg.pushConstantSize = sizeof(VoxelFrameRoot_t);
        meshCfg.depthWriteEnable = VK_TRUE;
        meshCfg.depthCompareOp = VK_COMPARE_OP_LESS;

        PipelineKey_t meshKey{};
        meshKey.taskHash = CPipelineCache::HashSpirV(taskSpv);
        meshKey.meshHash = CPipelineCache::HashSpirV(meshSpv);
        meshKey.fragHash = CPipelineCache::HashSpirV(fragSpv);
        meshKey.variants = PipelineVariant_MeshTask;
        meshKey.colorFmt = meshCfg.colorAttachmentFormat;
        meshKey.depthFmt = meshCfg.depthAttachmentFormat;
        meshKey.pushConstantSize = meshCfg.pushConstantSize;

        m_TaskMeshPipeline = CreateScope<CPipeline>(m_Context);
        cache.GetGraphics(meshKey, [&](VkPipelineCache) -> VkPipeline {
            m_TaskMeshPipeline->BuildMeshTask(taskSpv, meshSpv, fragSpv, meshCfg);
            return m_TaskMeshPipeline->GetHandle();
        });

        auto buildCompute = [&](Scope<CPipeline> &pipe, const std::vector<u8> &spv, u32 pushSize,
                                const char *name) {
            PipelineConfigParams_t cfg{};
            cfg.computeEntryPoint = "main";
            cfg.pushConstantSize = pushSize;
            PipelineKey_t key{};
            key.compHash = CPipelineCache::HashSpirV(spv);
            key.variants = PipelineVariant_Compute;
            key.pushConstantSize = pushSize;
            pipe = CreateScope<CPipeline>(m_Context);
            cache.GetCompute(key, [&](VkPipelineCache) -> VkPipeline {
                pipe->BuildCompute(spv, cfg);
                return pipe->GetHandle();
            });
            if (!pipe->GetHandle())
                LOG_ERROR("[CVoxelRenderer] Compute pipeline build failed: {}", name);
        };

        buildCompute(m_SpdPipeline, spdSpv, 48, "voxel_hiz_spd");
        buildCompute(m_EditPipeline, editSpv, sizeof(VoxelEditPushConstants_t), "voxel_edit");
        buildCompute(m_GiInjectPipeline, giInjectSpv, sizeof(VoxelGiPushConstants_t), "voxel_gi_inject");
        buildCompute(m_GiPropagatePipeline, giPropSpv, sizeof(VoxelGiPushConstants_t), "voxel_gi_propagate");
    }

    void CVoxelRenderer::CreateHiZ(u32 width, u32 height) {
        DestroyHiZ();
        m_HiZWidth = std::max(1u, width / 2);
        m_HiZHeight = std::max(1u, height / 2);
        m_HiZMips = 1 + static_cast<u32>(std::floor(std::log2(std::max(m_HiZWidth, m_HiZHeight))));

        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R32_SFLOAT;
        ii.extent = {m_HiZWidth, m_HiZHeight, 1};
        ii.mipLevels = m_HiZMips;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkDevice device = m_Context.GetDevice();
        if (vkCreateImage(device, &ii, nullptr, &m_HiZImage) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] HiZ image create failed");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, m_HiZImage, &req);
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(m_Context.GetPhysicalDevice(), &memProps);
        u32 memIdx = UINT32_MAX;
        for (u32 i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((req.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memIdx = i;
                break;
            }
        }
        if (memIdx == UINT32_MAX)
            throw std::runtime_error("[CVoxelRenderer] No HiZ memory type");
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memIdx;
        if (vkAllocateMemory(device, &ai, nullptr, &m_HiZMemory) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] HiZ alloc failed");
        vkBindImageMemory(device, m_HiZImage, m_HiZMemory, 0);

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_HiZImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R32_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, m_HiZMips, 0, 1};
        if (vkCreateImageView(device, &vi, nullptr, &m_HiZView) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] HiZ view create failed");
    }

    void CVoxelRenderer::DestroyHiZ() {
        VkDevice device = m_Context.GetDevice();
        if (!device)
            return;
        if (m_HiZView) {
            vkDestroyImageView(device, m_HiZView, nullptr);
            m_HiZView = VK_NULL_HANDLE;
        }
        if (m_HiZImage) {
            vkDestroyImage(device, m_HiZImage, nullptr);
            m_HiZImage = VK_NULL_HANDLE;
        }
        if (m_HiZMemory) {
            vkFreeMemory(device, m_HiZMemory, nullptr);
            m_HiZMemory = VK_NULL_HANDLE;
        }
    }

    void CVoxelRenderer::DispatchEdits(VkCommandBuffer cb) {
        const auto &edits = m_World->GetPendingEdits();
        if (edits.empty() || !m_EditPipeline->GetHandle())
            return;
        m_EditStaging->LoadData(edits.data(), sizeof(VoxelEditCmd_t) * edits.size());

        VoxelEditPushConstants_t pc{};
        pc.brickBufferAddr = m_World->GetBrickBufferAddr();
        pc.headerAddr = m_World->GetHeaderAddr();
        pc.pageTableAddr = m_World->GetPageTableAddr();
        pc.editAddr = m_EditStaging->GetDeviceAddress();
        pc.worldMin = m_World->GetWorldMin();
        pc.brickSize = m_World->GetBrickSize();
        pc.editCount = static_cast<u32>(edits.size());
        pc.virtualDim = m_World->GetVirtualDim();

        // Host -> compute visibility for the edit ring.
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.buffer = m_EditStaging->GetHandle();
        barrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cb, &dep);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_EditPipeline->GetHandle());
        vkCmdPushConstants(cb, m_EditPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc),
                           &pc);
        vkCmdDispatch(cb, (pc.editCount + 63) / 64, 1, 1);

        // Compute writes (brick pages, headers) -> task/mesh/fragment reads.
        // Global memory barrier since the sparse brick store is not a CBuffer.
        VkMemoryBarrier2 post{};
        post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        post.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        post.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        post.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                            VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        post.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        VkDependencyInfo postDep{};
        postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        postDep.memoryBarrierCount = 1;
        postDep.pMemoryBarriers = &post;
        vkCmdPipelineBarrier2(cb, &postDep);
    }

    void CVoxelRenderer::DispatchGi(VkCommandBuffer cb) {
        // GI cascade volumes are stubs; skip both dispatches + barrier
        // unless explicitly enabled (saves 2x 8x8x8 dispatches/frame).
        if (!m_bGiEnabled)
            return;
        if (!m_GiInjectPipeline->GetHandle() || !m_GiPropagatePipeline->GetHandle())
            return;
        VoxelGiPushConstants_t pc{};
        pc.brickBufferAddr = m_World->GetBrickBufferAddr();
        pc.headerAddr = m_World->GetHeaderAddr();
        pc.cascadeAddr = m_CascadeBuffer->GetDeviceAddress();
        pc.sunAddr = m_SunBuffer->GetDeviceAddress();
        pc.brickCount = m_World->GetBrickCount();
        pc.cascadeRes = 64;
        pc.cascadeCount = 3;
        pc.brickSize = m_World->GetBrickSize();
        pc.worldMin = m_World->GetWorldMin();

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_GiInjectPipeline->GetHandle());
        vkCmdPushConstants(cb, m_GiInjectPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(pc), &pc);
        vkCmdDispatch(cb, 8, 8, 8);

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
        vkCmdPipelineBarrier2(cb, &dep);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_GiPropagatePipeline->GetHandle());
        vkCmdPushConstants(cb, m_GiPropagatePipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(pc), &pc);
        vkCmdDispatch(cb, 8, 8, 8);
    }

    void CVoxelRenderer::Record(VkCommandBuffer cb, VkExtent2D extent, VkImageView colorView,
                                VkImageView depthView, bool clearColor, u32 flightSlot,
                                const Mat4 &viewProj, const Mat4 &prevViewProj,
                                const Vec3 &cameraPos, float nearZ, float farZ) {
        if (!m_bInitialized)
            return;
        (void)nearZ;
        (void)farZ;

        // All compute + uploads happen OUTSIDE any render pass (VUID
        // vkCmdDispatch-None-10672 / vkCmdPipelineBarrier2-None-09553).
        m_World->FlushHeaders();
        DispatchEdits(cb);
        DispatchGi(cb);
        m_World->ClearEdits();

        const u32 brickCount = m_World->GetBrickCount();
        m_Stats.brickCount = brickCount;
        m_Stats.editCount = 0;
        if (brickCount == 0 || !m_TaskMeshPipeline->GetHandle())
            return;

        // Pass 1: task-shader frustum + distance cull, HiZ re-projection uses
        // prevViewProj once the pyramid is populated (enableHiZ flipped when
        // the SPD chain has run at least once — v1 runs it unconditionally).
        // Push constant is the 40B frame root; frame params live in a BDA buffer.
        VoxelFrameParams_t frame{};
        frame.viewProj = viewProj;
        frame.prevViewProj = prevViewProj;
        frame.cameraPos = cameraPos;
        frame.brickCount = brickCount;
        frame.worldMin = m_World->GetWorldMin();
        frame.brickSize = m_World->GetBrickSize();
        frame.maxDrawDistance = 10000;
        frame.enableHiZ = 1;
        frame.paletteAddr = m_PaletteBuffer->GetDeviceAddress();
        frame.sunAddr = m_SunBuffer->GetDeviceAddress();
        frame.giAddr = 0; // GI stub adds light on black; v1 visibility = lambert only
        frame.giEnabled = m_bGiEnabled ? 1u : 0u;
        frame.shadowsEnabled = 0;
        frame.hizAddr = 0;
        frame.debugEnabled = m_bDebugEnabled ? 1u : 0u;
        CBuffer &paramsSlot = *m_FrameParamsRing[flightSlot % kFlightSlots];
        paramsSlot.LoadData(&frame, sizeof(frame));

        VoxelFrameRoot_t root{};
        root.brickBufferAddr = m_World->GetBrickBufferAddr();
        root.headerAddr = m_World->GetHeaderAddr();
        root.pageTableAddr = m_World->GetPageTableAddr();
        root.visibilityAddr = m_VisibilityBuffer->GetDeviceAddress();
        root.frameAddr = paramsSlot.GetDeviceAddress();
        // DEBUG counters: device-side clear only when capture is enabled.
        // Skipping the Fill + extra barrier saves a full-buffer op/frame.
        if (m_bDebugEnabled)
            vkCmdFillBuffer(cb, m_DebugReadback->GetHandle(), 0, sizeof(u32) * 8, 0);
        root.debugAddr = m_DebugReadback->GetDeviceAddress();

        // Frame params (host upload) -> task/mesh/fragment. Debug-clear
        // barrier is appended only when capture is enabled.
        {
            VkBufferMemoryBarrier2 b[2]{};
            u32 barrierCount = 1;
            b[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            b[0].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            b[0].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b[0].dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            b[0].buffer = paramsSlot.GetHandle();
            b[0].size = VK_WHOLE_SIZE;
            if (m_bDebugEnabled) {
                b[1] = b[0];
                b[1].buffer = m_DebugReadback->GetHandle();
                barrierCount = 2;
            }
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = barrierCount;
            dep.pBufferMemoryBarriers = b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        // Own voxel-only dynamic-rendering pass (shares the Sponza
        // offscreen+depth IMAGES but never runs inside the PBR pass).
        // clearColor=true on the first voxel pass of the frame (nothing drew
        // before us); false when appending after PBR/skybox.
        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.imageView = colorView;
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.clearValue.color = {{0.05f, 0.05f, 0.07f, 1.f}};

        VkRenderingAttachmentInfo depthAtt{};
        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAtt.imageView = depthView;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.clearValue.depthStencil = {1.f, 0};

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent = extent;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;
        ri.pDepthAttachment = &depthAtt;
        vkCmdBeginRendering(cb, &ri);

        VkViewport vp{0.f, 0.f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                      0.f, 1.f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D sc{{0, 0}, extent};
        vkCmdSetScissor(cb, 0, 1, &sc);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TaskMeshPipeline->GetHandle());
        vkCmdPushConstants(cb, m_TaskMeshPipeline->GetLayout(),
                           VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT |
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(root), &root);

        const u32 taskGroups = brickCount; // one task group per brick; task fans out x32 mesh
        m_Stats.taskGroups = taskGroups;
        vkCmdDrawMeshTasksEXT(cb, taskGroups, 1, 1);
        vkCmdEndRendering(cb);

        // DEBUG host barrier only when capture is enabled; otherwise skip
        // the extra pipeline stall entirely.
        if (m_bDebugEnabled) {
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
            vkCmdPipelineBarrier2(cb, &dep);
        }

        m_PrevViewProj = viewProj;

        // Pass 2 (HiZ occlusion resolution) + SPD pyramid rebuild are driven
        // from the depth image produced by this pass; the SPD compute reads
        // the depth attachment via the HiZ sampler set (follow-up diff).
    }

    void CVoxelRenderer::ReadDebugCounters(u32 out[6]) const {
        // Disabled => return last cached values with NO queue stall.
        if (!m_bDebugEnabled || !m_DebugReadback) {
            for (int i = 0; i < 6; ++i)
                out[i] = m_CachedDebug[i];
            return;
        }
        // GPU writes land in device memory; the validation app is the only
        // reader and runs single-frame-in-flight, so a queue idle + mapped
        // read is the correct (if slow) diagnostic path.
        vkQueueWaitIdle(m_Context.GetGraphicsQueue());
        vmaInvalidateAllocation(m_Context.GetAllocator(), m_DebugReadback->GetAllocation(), 0,
                                sizeof(u32) * 8);
        const auto *src = static_cast<const u32 *>(m_DebugReadback->GetMapped());
        for (int i = 0; i < 6; ++i) {
            out[i] = src[i];
            m_CachedDebug[i] = src[i];
        }
    }
} // namespace Manro
