#pragma once

#include "VoxelTypes.h"
#include "VoxelBlockAssets.h"
#include <Manro/Core/Types.h>
#include <Manro/Render/VoxelStreamStats.h>
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

        u32 dbgTaskRuns{0};
        u32 dbgVisible{0};
        u32 dbgFaces{0};
        u32 dbgCulled{0};
        u32 dbgMeshRuns{0};
        u32 dbgMeshFaces{0};

        float gpuXferMs{0.f};
        float gpuDrawMs{0.f};
        float gpuPostMs{0.f};
        float gpuXferInstMs{0.f};
        float gpuDrawInstMs{0.f};
        float gpuPostInstMs{0.f};
    };

    class CVoxelRenderer {
    public:
        CVoxelRenderer(CVulkanContext &ctx, CVirtualFS &vfs);
        ~CVoxelRenderer();

        CVoxelRenderer(const CVoxelRenderer &) = delete;
        CVoxelRenderer &operator=(const CVoxelRenderer &) = delete;

        void Init(CPipelineCache &cache, u32 width, u32 height);
        void Shutdown();

        void Record(VkCommandBuffer cb, VkExtent2D extent, VkImageView colorView,
                    VkImageView depthView, bool clearColor, u32 flightSlot, const Mat4 &viewProj,
                    const Mat4 &prevViewProj, const Vec3 &cameraPos, float nearZ, float farZ);

        Vec3 StreamInit(const std::string &worldDir, const std::string &assetsDir, int radiusSections);

        int StreamUpdate(const Vec3 &cameraPos, u32 flightSlot);

        [[nodiscard]] const VoxelStreamStats_t &GetStreamStats() const;

        bool StreamIsSolid(const Vec3 &p) const;
        bool StreamIsFluid(const Vec3 &p) const;

        u32 StreamPlaceState() const;

        void StreamApplyEdit(const Vec3 &pos, float radius, u32 op, u32 state = 0u);

        i32 StreamGetStateAt(const Vec3 &p) const;

        bool StreamGetCollisionBox(const Vec3 &p, Vec3 &mn, Vec3 &mx) const;
        std::string GetStateKey(u32 id) const;
        u32 FindStateByKey(const std::string &key) const;

        [[nodiscard]] CVoxelWorld &GetWorld() { return *m_World; }
        [[nodiscard]] const VoxelFrameStats_t &GetStats() const { return m_Stats; }

        void SetDebugEnabled(bool e) { m_bDebugEnabled = e; }
        void SetGiEnabled(bool e) { m_bGiEnabled = e; }

        void SetBackfaceEnabled(bool e) { m_bUseBackface = e; }
        void SetFrustumEnabled(bool e) { m_bUseFrustum = e; }

        void ReadDebugCounters(u32 out[6]) const;

        void CmdWriteTimestamp(VkCommandBuffer cb, u32 flightSlot, u32 subIdx);

    private:
        void BuildPipelines(CPipelineCache &cache);
        void CreateTileDescriptor();
        void DestroyTileDescriptor();
        void UploadBlockTables();
        void CreateBlockTiles(const BlockAssetPack_t &pack);
        void DestroyBlockTiles();
        void DispatchEdits(VkCommandBuffer cb);
        void DispatchGi(VkCommandBuffer cb);
        [[nodiscard]] VkDeviceAddress GetGiSampleAddr() const;

        CVulkanContext &m_Context;
        CVirtualFS &m_Vfs;

        Scope<CVoxelWorld> m_World;
        Scope<CPipeline> m_TaskMeshPipeline;
        Scope<CPipeline> m_EditPipeline;
        Scope<CPipeline> m_GiInjectPipeline;
        Scope<CPipeline> m_GiPropagatePipeline;

        Scope<CBuffer> m_BlockTileTable;
        Scope<CBuffer> m_BlockFlagsTable;
        Scope<CBuffer> m_BlockShapeTable;
        Scope<CBuffer> m_BlockUvTable;

        VkImage m_TileImage{VK_NULL_HANDLE};
        VkDeviceMemory m_TileMemory{VK_NULL_HANDLE};
        VkImageView m_TileView{VK_NULL_HANDLE};
        VkSampler m_TileSampler{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_TileSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_TilePool{VK_NULL_HANDLE};
        VkDescriptorSet m_TileSet{VK_NULL_HANDLE};
        u32 m_TileLayers{0};

        BlockAssetPack_t m_BlockPack;
        std::unique_ptr<CVoxelStreamWorld> m_StreamWorld;

        static constexpr u32 kFlightSlots = 3;
        std::array<Scope<CBuffer>, kFlightSlots> m_FrameParamsRing{};

        std::array<Scope<CBuffer>, kFlightSlots> m_VisibilityRing{};
        Scope<CBuffer> m_PaletteBuffer;
        Scope<CBuffer> m_SunBuffer;
        Scope<CBuffer> m_FragParams;
        Vec4 m_SunDir{0.f};
        Vec4 m_SunColor{1.f};
        Scope<CBuffer> m_CascadeBuffer;
        Scope<CBuffer> m_EditStaging;
        Scope<CBuffer> m_DebugReadback;
        Scope<CBuffer> m_FaceCache;

        std::vector<u32> m_VisibleList;
        std::vector<std::pair<float, u32>> m_SortScratch;

        Mat4 m_CachedViewProj{0.f};
        Vec3 m_CachedCameraPos{0.f};
        u32 m_CachedBrickCount{~0u};
        u64 m_CachedHeaderVersion{~0ull};
        bool m_CachedUseFrustum{false};
        bool m_VisCacheValid{false};

        u64 m_RecordTick{0};

        static constexpr u32 kQueriesPerSlot = 6;
        VkQueryPool m_TimestampPool{VK_NULL_HANDLE};
        float m_TimestampPeriodNs{0.f};

        Mat4 m_PrevViewProj{1.f};
        VoxelFrameStats_t m_Stats{};
        bool m_bInitialized{false};
        bool m_bDebugEnabled{false};
        bool m_bGiEnabled{false};
        bool m_bUseBackface{true};
        bool m_bUseFrustum{true};
        mutable u32 m_CachedDebug[6]{};
    };
}
