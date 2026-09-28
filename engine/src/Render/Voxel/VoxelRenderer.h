#pragma once

// CVoxelRenderer: separate GPU-driven voxel path (task/mesh + BDA + sparse).
// Does NOT use CInstanceBatcher / GpuCullDispatcher / PBR pipelines.
// Owns: sparse world, task/mesh PSO, HiZ pyramid, edit + GI compute, BLAS stub.

#include "VoxelTypes.h"
#include <Manro/Core/Types.h>
#include <volk.h>

#include <array>

namespace Manro {
    class CVulkanContext;
    class CVirtualFS;
    class CPipeline;
    class CPipelineCache;
    class CBuffer;
    class CVoxelWorld;
    class CVoxelSparseBinder;

    struct VoxelFrameStats_t {
        u32 brickCount{0};
        u32 taskGroups{0};
        u32 editCount{0};
        // DEBUG counters read back from GPU (see Record tail).
        u32 dbgTaskRuns{0};
        u32 dbgVisible{0};
        u32 dbgFaces{0};
        u32 dbgCulled{0};
        u32 dbgMeshRuns{0};
        u32 dbgMeshFaces{0};
    };

    class CVoxelRenderer {
    public:
        CVoxelRenderer(CVulkanContext &ctx, CVirtualFS &vfs);
        ~CVoxelRenderer();

        CVoxelRenderer(const CVoxelRenderer &) = delete;
        CVoxelRenderer &operator=(const CVoxelRenderer &) = delete;

        void Init(CPipelineCache &cache, u32 width, u32 height);
        void Shutdown();

        // Per-frame record into an externally-owned command buffer.
        // viewProj/prevViewProj/camera drive the task-shader cull (two-pass HiZ
        // re-projection uses prevViewProj).
        // flightSlot selects the frame-params ring entry and MUST be the
        // engine's frame-in-flight index: the params buffer is host-written
        // every frame, so sharing one buffer across in-flight frames lets
        // frame N+1's upload tear frame N's in-flight task/mesh reads
        // (flickering holes / flaky counters).
        // MUST be called OUTSIDE any vkCmdBeginRendering pass: it runs compute
        // (edits/GI), uploads frame params, then issues vkCmdDrawMeshTasksEXT
        // inside its own voxel-only rendering pass over the given attachments.
        void Record(VkCommandBuffer cb, VkExtent2D extent, VkImageView colorView,
                    VkImageView depthView, bool clearColor, u32 flightSlot, const Mat4 &viewProj,
                    const Mat4 &prevViewProj,
                    const Vec3 &cameraPos, float nearZ, float farZ);

        [[nodiscard]] CVoxelWorld &GetWorld() { return *m_World; }
        [[nodiscard]] const VoxelFrameStats_t &GetStats() const { return m_Stats; }

        // DEBUG counters are opt-in: when disabled Record skips the Fill,
        // shader atomics (via frame.debugEnabled) and host barrier/WaitIdle.
        void SetDebugEnabled(bool e) { m_bDebugEnabled = e; }
        void SetGiEnabled(bool e) { m_bGiEnabled = e; }

        // DEBUG: host-visible copy of GPU counters (valid after WaitIdle or
        // after the frame's fence signals; call post-present for prior frame).
        void ReadDebugCounters(u32 out[6]) const;

    private:
        void BuildPipelines(CPipelineCache &cache);
        void CreateHiZ(u32 width, u32 height);
        void DestroyHiZ();
        void DispatchEdits(VkCommandBuffer cb);
        void DispatchGi(VkCommandBuffer cb);

        CVulkanContext &m_Context;
        CVirtualFS &m_Vfs;

        Scope<CVoxelWorld> m_World;
        Scope<CPipeline> m_TaskMeshPipeline;
        Scope<CPipeline> m_SpdPipeline;
        Scope<CPipeline> m_EditPipeline;
        Scope<CPipeline> m_GiInjectPipeline;
        Scope<CPipeline> m_GiPropagatePipeline;

        // Visibility + HiZ targets (separate from Sponza offscreen/depth).
        VkImage m_HiZImage{VK_NULL_HANDLE};
        VkImageView m_HiZView{VK_NULL_HANDLE};
        VkDeviceMemory m_HiZMemory{VK_NULL_HANDLE};
        u32 m_HiZMips{1};
        u32 m_HiZWidth{0};
        u32 m_HiZHeight{0};

        Scope<CBuffer> m_VisibilityBuffer; // uint2 per brick
        Scope<CBuffer> m_TaskIndirectBuffer; // VkDispatchIndirectCommand for pass 2
        Scope<CBuffer> m_TaskCountBuffer;
        // Frame-params ring: one host-written buffer per frame in flight
        // (see Record). Must cover the engine's maxFramesInFlight (3).
        static constexpr u32 kFlightSlots = 3;
        std::array<Scope<CBuffer>, kFlightSlots> m_FrameParamsRing{};
        Scope<CBuffer> m_PaletteBuffer; // float4[512]
        Scope<CBuffer> m_SunBuffer; // float4[2]
        Scope<CBuffer> m_CascadeBuffer; // float4[cascadeRes^3 * cascadeCount]
        Scope<CBuffer> m_EditStaging; // VoxelEditCmd_t ring mirror
        Scope<CBuffer> m_DebugReadback; // DEBUG: host-visible task/mesh counters

        Mat4 m_PrevViewProj{1.f};
        VoxelFrameStats_t m_Stats{};
        bool m_bInitialized{false};
        bool m_bDebugEnabled{false};
        bool m_bGiEnabled{false};
        mutable u32 m_CachedDebug[6]{};
    };
} // namespace Manro
