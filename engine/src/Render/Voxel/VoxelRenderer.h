#pragma once

// CVoxelRenderer: separate GPU-driven voxel path (task/mesh + BDA + sparse).
// Does NOT use CInstanceBatcher / GpuCullDispatcher / PBR pipelines.
// Owns: sparse world, task/mesh PSO, edit + GI compute, BLAS stub.
// Occlusion is hardware ZCULL + front-to-back sort + early-z; no software
// HiZ pyramid (measured slower than nothing: build cost, zero savings at
// occ~=0, bursty lows).

#include "VoxelTypes.h"
#include "VoxelBlockAssets.h"
#include <Manro/Core/Types.h>
#include <volk.h>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Manro {
    class CVulkanContext;
    class CVirtualFS;
    class CPipeline;
    class CPipelineCache;
    class CBuffer;
    class CVoxelWorld;
    class CVoxelSparseBinder;
    class CVoxelStreamWorld;

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
        // viewProj/prevViewProj/camera drive the task-shader cull.
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
                    const Mat4 &prevViewProj, const Vec3 &cameraPos, float nearZ, float farZ);

        // Streamed path: builds the block pack from the build-time
        // client-jar assets (textures), uploads the tile array + BDA tables,
        // and opens the Anvil save under worldDir (required — no fallback).
        // Returns the spawn position (level.dat, else a default).
        Vec3 StreamInit(const std::string &worldDir, const std::string &assetsDir, int radiusSections);
        // Streams sections around the camera (nearest-first fill, eviction
        // past the hysteresis ring). Returns unfilled remainder.
        // flightSlot is the frame-in-flight index: staged brick fills are
        // buffered into that slot's staging ring and copied in-frame by
        // Record (same slot), so no extra queue submit or fence wait.
        int StreamUpdate(const Vec3 &cameraPos, u32 flightSlot);
        // Physics queries against the streamed world (block coords floored
        // from p). Unloaded reads solid (see CVoxelStreamWorld).
        bool StreamIsSolid(const Vec3 &p) const;
        bool StreamIsFluid(const Vec3 &p) const;
        // Block state id for player block placement.
        u32 StreamPlaceState() const;
        // CPU-mirror half of a gameplay edit (see CVoxelStreamWorld::ApplyEdit).
        // The GPU half is queued in the world edit ring and consumed by the
        // edit compute dispatch; both must run for visuals + physics to agree.
        // state is the placed block state id (op == 1 only).
        void StreamApplyEdit(const Vec3 &pos, float radius, u32 op, u32 state = 0u);
        // Block state id at a world position, or -1 when unknown/unfilled.
        i32 StreamGetStateAt(const Vec3 &p) const;
        // Canonical "name|k=v;..." key for a state id ("" when unknown), and
        // the reverse lookup (~0u when the key has no state). Powers
        // pick-block + oriented placement in the sample.
        std::string GetStateKey(u32 id) const;
        u32 FindStateByKey(const std::string &key) const;

        [[nodiscard]] CVoxelWorld &GetWorld() { return *m_World; }
        [[nodiscard]] const VoxelFrameStats_t &GetStats() const { return m_Stats; }

        // DEBUG counters are opt-in: when disabled Record skips the Fill,
        // shader atomics (via frame.debugEnabled) and host barrier/WaitIdle.
        void SetDebugEnabled(bool e) { m_bDebugEnabled = e; }
        void SetGiEnabled(bool e) { m_bGiEnabled = e; }
        // Culling-stage kill switches (bisection + perf comparison).
        void SetBackfaceEnabled(bool e) { m_bUseBackface = e; }
        void SetFrustumEnabled(bool e) { m_bUseFrustum = e; }

        // DEBUG: host-visible copy of GPU counters (valid after WaitIdle or
        // after the frame's fence signals; call post-present for prior frame).
        void ReadDebugCounters(u32 out[6]) const;

    private:
        void BuildPipelines(CPipelineCache &cache);
        void CreateTileDescriptor();
        void DestroyTileDescriptor();
        void UploadBlockTables();
        void CreateBlockTiles(const BlockAssetPack_t &pack);
        void DestroyBlockTiles();
        void DispatchEdits(VkCommandBuffer cb);
        void DispatchGi(VkCommandBuffer cb);

        CVulkanContext &m_Context;
        CVirtualFS &m_Vfs;

        Scope<CVoxelWorld> m_World;
        Scope<CPipeline> m_TaskMeshPipeline;
        Scope<CPipeline> m_EditPipeline;
        Scope<CPipeline> m_GiInjectPipeline;
        Scope<CPipeline> m_GiPropagatePipeline;

        Scope<CBuffer> m_TaskIndirectBuffer; // VkDispatchIndirectCommand for pass 2
        Scope<CBuffer> m_TaskCountBuffer;
        // Block tile tables (BDA): tile layer per (state, face), flags per
        // state, shape AABB per state. Sized 32768 so any uint16 state indexes safely.
        Scope<CBuffer> m_BlockTileTable;
        Scope<CBuffer> m_BlockFlagsTable;
        Scope<CBuffer> m_BlockShapeTable;
        Scope<CBuffer> m_BlockUvTable;
        // Block tile texture array (descriptor-bound: images can't use
        // BDA). 16x16 sRGB tiles + CPU-generated mip chain, NEAREST mag.
        VkImage m_TileImage{VK_NULL_HANDLE};
        VkDeviceMemory m_TileMemory{VK_NULL_HANDLE};
        VkImageView m_TileView{VK_NULL_HANDLE};
        VkSampler m_TileSampler{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_TileSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_TilePool{VK_NULL_HANDLE};
        VkDescriptorSet m_TileSet{VK_NULL_HANDLE};
        u32 m_TileLayers{0};
        // Save-backed world source.
        BlockAssetPack_t m_BlockPack;
        std::unique_ptr<CVoxelStreamWorld> m_StreamWorld;
        // Frame-params ring: one host-written buffer per frame in flight
        // (see Record). Must cover the engine's maxFramesInFlight (3).
        static constexpr u32 kFlightSlots = 3;
        std::array<Scope<CBuffer>, kFlightSlots> m_FrameParamsRing{};
        // Sorted visible ordinals (uint per brick), host-written every frame.
        // Ringed like frame params: a single buffer would let frame N+1's
        // upload tear frame N's in-flight task reads (wrong bricks for a
        // frame — visible as dragging/ghosting while the camera moves).
        std::array<Scope<CBuffer>, kFlightSlots> m_VisibilityRing{};
        Scope<CBuffer> m_PaletteBuffer; // float4[512]
        Scope<CBuffer> m_SunBuffer; // float4[2]
        Scope<CBuffer> m_CascadeBuffer; // float4[cascadeRes^3 * cascadeCount]
        Scope<CBuffer> m_EditStaging; // VoxelEditCmd_t ring mirror
        Scope<CBuffer> m_DebugReadback; // DEBUG: host-visible task/mesh counters
        Scope<CBuffer> m_FaceCache; // uint[4096] per resident brick (faceCount + faces)
        // CPU sort scratch: front-to-back visible ordinals uploaded to the
        // visibility buffer each frame (exact dispatch, early-z order).
        std::vector<std::pair<float, u32>> m_VisibleScratch;
        std::vector<u32> m_VisibleList;

        Mat4 m_PrevViewProj{1.f};
        VoxelFrameStats_t m_Stats{};
        bool m_bInitialized{false};
        bool m_bDebugEnabled{false};
        bool m_bGiEnabled{false};
        bool m_bUseBackface{true};
        bool m_bUseFrustum{true};
        mutable u32 m_CachedDebug[6]{};
    };
} // namespace Manro
