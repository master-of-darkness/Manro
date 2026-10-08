#pragma once

#include "VoxelTypes.h"
#include <volk.h>
#include <array>
#include <vector>

namespace Manro {
    class CVulkanContext;
    class CBuffer;
    class CVoxelSparseBinder;

    struct VoxelWorldDesc_t {
        u32 virtualDim{64};
        float voxelSize{1.f};
        Vec3 worldMin{0.f, 0.f, 0.f};
        u32 maxResidentBricks{8192};
    };

    class CVoxelWorld {
    public:
        explicit CVoxelWorld(CVulkanContext &ctx);
        ~CVoxelWorld();

        CVoxelWorld(const CVoxelWorld &) = delete;
        CVoxelWorld &operator=(const CVoxelWorld &) = delete;

        void Init(const VoxelWorldDesc_t &desc);
        void Shutdown();

        i32 AllocateBrick(u32 bx, u32 by, u32 bz);

        i32 AllocateBrickSlot(u32 slot, const Vec3 &origin);

        void EvictBrick(u32 brickIdx);

        void ReserveResident(u32 brickCount);

        void UploadBrickData(u32 brickIdx, const u16 *mats, const u32 *occupancy);

        void UploadBrickBatch(const u32 *indices, const u16 *mats, const u32 *occupancy, u32 count);

        bool StageBrickBatch(u32 slot, const u32 *indices, const u16 *mats, const u32 *occupancy,
                             u32 count);

        void FlushStagedUploads(VkCommandBuffer cb, u32 slot);
        [[nodiscard]] VkBuffer GetBrickStoreHandle() const;

        void SetVoxel(u32 bx, u32 by, u32 bz, u32 lx, u32 ly, u32 lz, u16 mat);

        void MarkBrickDirty(u32 brickIdx);

        void MarkBrickAndNeighborsDirty(u32 brickIdx);

        void SetBrickOpaqueFull(u32 brickIdx, bool opaque);

        [[nodiscard]] bool IsBrickHidden(u32 brickIdx) const {
            return brickIdx < m_BrickHidden.size() && m_BrickHidden[brickIdx] != 0;
        }

        void QueueEdit(const VoxelEditCmd_t &cmd);
        void ClearEdits();

        [[nodiscard]] VkDeviceAddress GetBrickBufferAddr() const;
        [[nodiscard]] VkDeviceAddress GetHeaderAddr() const;
        [[nodiscard]] VkDeviceAddress GetPageTableAddr() const;

        [[nodiscard]] u32 GetBrickCount() const { return m_BrickCount; }
        [[nodiscard]] float GetBrickSize() const { return m_BrickSize; }
        [[nodiscard]] const Vec3 &GetWorldMin() const { return m_WorldMin; }

        void SetWorldMin(const Vec3 &min) { m_WorldMin = min; }
        [[nodiscard]] u32 GetVirtualDim() const { return m_VirtualDim; }
        [[nodiscard]] const std::vector<VoxelEditCmd_t> &GetPendingEdits() const { return m_PendingEdits; }

        [[nodiscard]] const std::vector<VoxelBrickHeader_t> &GetHeaderMirror() const {
            return m_HeaderMirror;
        }

        void FlushHeaders();

    private:
        u32 BrickSlot(u32 bx, u32 by, u32 bz) const { return bx + by * m_VirtualDim + bz * m_VirtualDim * m_VirtualDim; }

        i32 SlotNeighbor(u32 slot, int dx, int dy, int dz) const;

        i32 NeighborBrick(u32 brickIdx, int dx, int dy, int dz) const;
        void RecomputeHidden(u32 brickIdx);

        void InvalidateHiddenNear(const Vec3 &pos, float radius);

        CVulkanContext *m_Context{nullptr};
        Scope<CVoxelSparseBinder> m_Bricks;
        Scope<CBuffer> m_Headers;
        Scope<CBuffer> m_PageTable;
        Scope<CBuffer> m_Palette;
        Scope<CBuffer> m_EditRing;

        std::vector<VoxelBrickHeader_t> m_HeaderMirror;
        std::vector<i32> m_PageMirror;
        std::vector<VoxelEditCmd_t> m_PendingEdits;

        std::vector<u32> m_BrickSlots;
        std::vector<u8> m_BrickOpaqueFull;
        std::vector<u8> m_BrickHidden;

        std::vector<u32> m_FreeBricks;

        static constexpr u32 kStageSlots = 3;
        static constexpr u32 kStageCapacityBricks = 64;
        Scope<CBuffer> m_UploadStaging;
        std::array<u32, kStageSlots> m_StageCounts{};
        std::array<std::array<u32, kStageCapacityBricks>, kStageSlots> m_StageIndices{};

        u32 m_VirtualDim{64};
        float m_VoxelSize{1.f};
        float m_BrickSize{16.f};
        Vec3 m_WorldMin{0.f};
        u32 m_BrickCount{0};

        std::vector<u32> m_DirtyHeaders;

        bool m_bPagesDirty{true};

        bool m_bHeadersFullUpload{true};
    };
}
