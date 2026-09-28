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

        // Sorted visible ordinals (uint per brick), host-written every frame.
        m_VisibilityBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 2 * desc.maxResidentBricks,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
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
        // Face cache: uint[2049] per resident brick (count + packed faces).
        // Written by task on first sight / edits, read by task (count) +
        // mesh (faces) every frame. 8192 bricks x 2049 x 4B = 64MB GPU-only.
        m_FaceCache = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 2049 * desc.maxResidentBricks,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
        // DEBUG counters: uint[8], device-cleared when capture is enabled,
        // atomically incremented by task/mesh. Host-visible for diagnosis.
        m_DebugReadback = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 8,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        // Sun dir normalized once here so the fragment shader (SM-bound at
        // fullscreen) can use it directly with no per-pixel normalize.
        Vec4 sun[2] = {Vec4(0.3f, -1.f, 0.2f, 0.f), Vec4(1.f, 0.98f, 0.9f, 3.f)};
        {
            const float len =
                std::sqrt(sun[0].x * sun[0].x + sun[0].y * sun[0].y + sun[0].z * sun[0].z);
            if (len > 1e-6f) {
                sun[0].x /= len;
                sun[0].y /= len;
                sun[0].z /= len;
            }
        }
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
        LOG_INFO("[CVoxelRenderer] Initialized (sparse bricks, task/mesh PSO, {}x{})", width, height);
    }

    void CVoxelRenderer::Shutdown() {
        VkDevice device = m_Context.GetDevice();
        if (device) {
            vkDeviceWaitIdle(device);
        }
        m_TaskMeshPipeline.reset();
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
        m_FaceCache.reset();
        if (m_World)
            m_World->Shutdown();
        m_bInitialized = false;
    }

    void CVoxelRenderer::BuildPipelines(CPipelineCache &cache) {
        auto taskSpv = m_Vfs.ReadFile("shaders://voxel_task.task.spv");
        auto meshSpv = m_Vfs.ReadFile("shaders://voxel_mesh.mesh.spv");
        auto fragSpv = m_Vfs.ReadFile("shaders://voxel_shade.frag.spv");
        auto editSpv = m_Vfs.ReadFile("shaders://voxel_edit.comp.spv");
        auto giInjectSpv = m_Vfs.ReadFile("shaders://voxel_gi_inject.comp.spv");
        auto giPropSpv = m_Vfs.ReadFile("shaders://voxel_gi_propagate.comp.spv");

        if (taskSpv.empty() || meshSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel task/mesh shaders missing");
        if (fragSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel shade fragment missing");
        if (editSpv.empty() || giInjectSpv.empty() || giPropSpv.empty())
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

        buildCompute(m_EditPipeline, editSpv, sizeof(VoxelEditPushConstants_t), "voxel_edit");
        buildCompute(m_GiInjectPipeline, giInjectSpv, sizeof(VoxelGiPushConstants_t), "voxel_gi_inject");
        buildCompute(m_GiPropagatePipeline, giPropSpv, sizeof(VoxelGiPushConstants_t), "voxel_gi_propagate");
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

        // CPU front-to-back sort + frustum compact (the proper overdraw cut):
        // visible bricks only, nearest first, so hardware ZCULL + early-z
        // kills hidden pixels instead of shading them. 128 bricks is
        // microseconds; dispatch gets the exact visible count — no empty
        // task groups. Same NDC convention the mesh shader rasterizes with
        // (glm M*v), so the compact can never disagree with the draw.
        u32 visibleCount = brickCount;
        {
            const float brickSize = m_World->GetBrickSize();
            const float radius = brickSize * 0.8660254f; // half-diagonal
            const float maxD = 10000.f + radius;
            m_VisibleScratch.clear();
            if (m_bUseFrustum) {
                const auto &mirror = m_World->GetHeaderMirror();
                for (u32 i = 0; i < brickCount; ++i) {
                    const VoxelBrickHeader_t &h = mirror[i];
                    if ((h.flags & 1u) == 0u)
                        continue;
                    const Vec3 center = h.origin + Vec3(brickSize * 0.5f);
                    const Vec3 toC = center - cameraPos;
                    const float dist2 = glm::dot(toC, toC);
                    if (dist2 > maxD * maxD)
                        continue;
                    const bool inBox =
                        cameraPos.x >= h.origin.x && cameraPos.y >= h.origin.y &&
                        cameraPos.z >= h.origin.z && cameraPos.x <= h.origin.x + brickSize &&
                        cameraPos.y <= h.origin.y + brickSize && cameraPos.z <= h.origin.z + brickSize;
                    bool hit = inBox, anyFront = false, anyBehind = false;
                    if (!inBox) {
                        for (u32 c = 0; c < 8u && !hit; ++c) {
                            const Vec3 corner =
                                h.origin +
                                Vec3(float(c & 1u), float((c >> 1u) & 1u), float((c >> 2u) & 1u)) *
                                    brickSize;
                            const Vec4 clip = viewProj * Vec4(corner, 1.f);
                            if (clip.w > 0.f) {
                                anyFront = true;
                                if (std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w &&
                                    clip.z >= 0.f && clip.z <= clip.w)
                                    hit = true;
                            } else {
                                anyBehind = true;
                            }
                        }
                        if (!hit && !(anyFront && anyBehind))
                            continue; // culled
                    }
                    m_VisibleScratch.emplace_back(dist2, i);
                }
                std::sort(m_VisibleScratch.begin(), m_VisibleScratch.end(),
                          [](const auto &a, const auto &b) { return a.first < b.first; });
                visibleCount = static_cast<u32>(m_VisibleScratch.size());
            } else {
                for (u32 i = 0; i < brickCount; ++i)
                    m_VisibleScratch.emplace_back(0.f, i);
            }
            m_VisibleList.resize(m_VisibleScratch.size());
            for (size_t k = 0; k < m_VisibleScratch.size(); ++k)
                m_VisibleList[k] = m_VisibleScratch[k].second;
            if (!m_VisibleList.empty())
                m_VisibilityBuffer->LoadData(m_VisibleList.data(),
                                             sizeof(u32) * m_VisibleList.size());
        }

        // Push constant is the frame root; frame params live in a BDA buffer.
        VoxelFrameParams_t frame{};
        frame.viewProj = viewProj;
        frame.prevViewProj = prevViewProj;
        frame.cameraPos = cameraPos;
        frame.brickCount = brickCount;
        frame.worldMin = m_World->GetWorldMin();
        frame.brickSize = m_World->GetBrickSize();
        frame.maxDrawDistance = 10000;
        frame.enableHiZ = 0; // reserved: software HiZ removed (ZCULL handles it)
        frame.paletteAddr = m_PaletteBuffer->GetDeviceAddress();
        frame.sunAddr = m_SunBuffer->GetDeviceAddress();
        frame.giAddr = 0; // GI stub adds light on black; v1 visibility = lambert only
        frame.giEnabled = m_bGiEnabled ? 1u : 0u;
        frame.shadowsEnabled = 0;
        frame.debugEnabled = m_bDebugEnabled ? 1u : 0u;
        frame.useBackface = m_bUseBackface ? 1u : 0u;
        frame.useFrustum = m_bUseFrustum ? 1u : 0u;
        CBuffer &paramsSlot = *m_FrameParamsRing[flightSlot % kFlightSlots];
        paramsSlot.LoadData(&frame, sizeof(frame));

        VoxelFrameRoot_t root{};
        root.brickBufferAddr = m_World->GetBrickBufferAddr();
        root.headerAddr = m_World->GetHeaderAddr();
        root.pageTableAddr = m_World->GetPageTableAddr();
        root.visibilityAddr = m_VisibilityBuffer->GetDeviceAddress();
        root.frameAddr = paramsSlot.GetDeviceAddress();
        root.faceCacheAddr = m_FaceCache->GetDeviceAddress();
        // DEBUG counters: device-side clear only when capture is enabled.
        // Skipping the Fill + extra barrier saves a full-buffer op/frame.
        if (m_bDebugEnabled)
            vkCmdFillBuffer(cb, m_DebugReadback->GetHandle(), 0, sizeof(u32) * 8, 0);
        root.debugAddr = m_DebugReadback->GetDeviceAddress();

        // Frame params + visibility list (host uploads) -> task/mesh.
        // Debug-clear barrier appended only when capture is enabled. The
        // global memory barrier orders prior in-flight frames' shader
        // writes (face-cache stores, edit writes) before task reads.
        {
            VkBufferMemoryBarrier2 b[3]{};
            u32 barrierCount = 2;
            b[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            b[0].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            b[0].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b[0].dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            b[0].buffer = paramsSlot.GetHandle();
            b[0].size = VK_WHOLE_SIZE;
            b[1] = b[0];
            b[1].buffer = m_VisibilityBuffer->GetHandle();
            if (m_bDebugEnabled) {
                b[2] = b[0];
                b[2].buffer = m_DebugReadback->GetHandle();
                barrierCount = 3;
            }
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = barrierCount;
            dep.pBufferMemoryBarriers = b;
            // Cross-frame visibility: prior frames' task/mesh/compute writes
            // (face cache, brick edits) -> this frame's task/mesh reads.
            VkMemoryBarrier2 cross{};
            cross.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            cross.srcStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                 VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                 VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            cross.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
            cross.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                 VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            cross.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            dep.memoryBarrierCount = 1;
            dep.pMemoryBarriers = &cross;
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

        // Sorted front-to-back compact list: exact dispatch count, no empty
        // task groups, early-z order for hidden-pixel rejection.
        m_Stats.taskGroups = visibleCount;
        if (visibleCount > 0)
            vkCmdDrawMeshTasksEXT(cb, visibleCount, 1, 1);
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
                                sizeof(u32) * 6);
        const auto *src = static_cast<const u32 *>(m_DebugReadback->GetMapped());
        for (int i = 0; i < 6; ++i) {
            out[i] = src[i];
            m_CachedDebug[i] = src[i];
        }
    }
} // namespace Manro
