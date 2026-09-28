#pragma once

// CVoxelWorld: sparse brickmap ownership + streaming + edit queue.
// - Virtual space: virtualDim^3 brick slots, page table int32 (-1 unbound).
// - Backing: CVoxelSparseBinder (true sparse, device-local pages).
// - Headers/palette/edit ring: dense CBuffers (CPU-visible).
// - Mesh/task shaders traverse via BDA; CPU never re-meshes.

#include "VoxelTypes.h"
#include <volk.h>
#include <vector>

namespace Manro {
    class CVulkanContext;
    class CBuffer;
    class CVoxelSparseBinder;

    struct VoxelWorldDesc_t {
        u32 virtualDim{64}; // bricks per axis (virtual address space)
        float voxelSize{1.f};
        Vec3 worldMin{0.f, 0.f, 0.f};
        u32 maxResidentBricks{8192}; // cap on bound sparse pages
    };

    class CVoxelWorld {
    public:
        explicit CVoxelWorld(CVulkanContext &ctx);
        ~CVoxelWorld();

        CVoxelWorld(const CVoxelWorld &) = delete;
        CVoxelWorld &operator=(const CVoxelWorld &) = delete;

        void Init(const VoxelWorldDesc_t &desc);
        void Shutdown();

        // Reserve + bind sparse pages for a brick slot; returns brick index or -1.
        i32 AllocateBrick(u32 bx, u32 by, u32 bz);

        // Reserve physical sparse pages for `brickCount` bricks in ONE
        // bind (one vkQueueBindSparse + one queue wait). Without this, the
        // per-brick BindRange inside AllocateBrick stalls the queue ~580
        // times during world init (~300ms). Later AllocateBrick calls then
        // find their pages resident and skip queue work entirely.
        // Call before the allocate loop with the final brick total (clamped
        // to the resident budget internally).
        void ReserveResident(u32 brickCount);

        // Fill brick voxel data (CPU staging -> device copy via one-shot).
        // mats: 4096 uint16, occ edited via SetVoxel or full upload.
        void UploadBrickData(u32 brickIdx, const u16 *mats, const u32 *occupancy);

        // Batched fill: ONE staging buffer + ONE one-shot submit for `count`
        // bricks (indices[i] <- mats[i*4096], occ[i*128]). The per-brick
        // UploadBrickData above stalls the queue once per brick (fence wait
        // + submit + wait); streaming 32 sections/frame that way costs ~70ms.
        // Batching cuts it to a single sync point.
        void UploadBrickBatch(const u32 *indices, const u16 *mats, const u32 *occupancy, u32 count);

        // Single-voxel CPU write (staging path for world-gen; gameplay uses GPU edits).
        void SetVoxel(u32 bx, u32 by, u32 bz, u32 lx, u32 ly, u32 lz, u16 mat);

        // Marks a brick's header dirty (task-shader bit 1) so the face cache
        // rescans it next frame. REQUIRED after every CPU fill: a brick
        // scanned while empty caches 0 faces + valid and would otherwise
        // stay invisible forever (only a GPU edit would rescan it).
        void MarkBrickDirty(u32 brickIdx);
        // Marks a brick AND its 6 face-neighbors dirty (see MarkBrickDirty).
        // REQUIRED after a fill whose voxels can hide/show neighbor boundary
        // faces: without it early-filled bricks keep stale boundary faces
        // against later-filled solid neighbors (hidden overdraw forever).
        void MarkBrickAndNeighborsDirty(u32 brickIdx);
        // Fully-opaque tracking for hidden-brick culling (see below).
        // Set by the world source after filling a brick: true when every one
        // of the 4096 voxels is an opaque occluder (no air/cutout/skip).
        void SetBrickOpaqueFull(u32 brickIdx, bool opaque);
        // True when the brick AND all 6 face-neighbors are opaque-full, i.e.
        // every face the task shader would emit is hidden inside solid rock.
        // Such bricks are skipped before dispatch (no task/mesh/raster cost).
        // CPU-coherent with edits: QueueEdit unhides the affected neighborhood.
        [[nodiscard]] bool IsBrickHidden(u32 brickIdx) const {
            return brickIdx < m_BrickHidden.size() && m_BrickHidden[brickIdx] != 0;
        }
        // Queue a GPU-local edit (SphereEdit). Consumed by the edit compute dispatch.
        void QueueEdit(const VoxelEditCmd_t &cmd);
        void ClearEdits();

        // BDA addresses for VoxelPushConstants_t.
        [[nodiscard]] VkDeviceAddress GetBrickBufferAddr() const;
        [[nodiscard]] VkDeviceAddress GetHeaderAddr() const;
        [[nodiscard]] VkDeviceAddress GetPageTableAddr() const;

        [[nodiscard]] u32 GetBrickCount() const { return m_BrickCount; }
        [[nodiscard]] float GetBrickSize() const { return m_BrickSize; }
        [[nodiscard]] const Vec3 &GetWorldMin() const { return m_WorldMin; }
        // Must be called before any AllocateBrick (origins are alloc-time).
        void SetWorldMin(const Vec3 &min) { m_WorldMin = min; }
        [[nodiscard]] u32 GetVirtualDim() const { return m_VirtualDim; }
        [[nodiscard]] const std::vector<VoxelEditCmd_t> &GetPendingEdits() const { return m_PendingEdits; }
        // CPU mirror of brick headers (origins/flags) for the front-to-back
        // sort + frustum compact in Record. Origins are alloc-time constant.
        [[nodiscard]] const std::vector<VoxelBrickHeader_t> &GetHeaderMirror() const {
            return m_HeaderMirror;
        }

        // Sync headers + page table to GPU after allocation/upload.
        void FlushHeaders();

    private:
        u32 BrickSlot(u32 bx, u32 by, u32 bz) const { return bx + by * m_VirtualDim + bz * m_VirtualDim * m_VirtualDim; }
        // Resident brick index of the face-neighbor brick (dx,dy,dz), or -1.
        i32 NeighborBrick(u32 brickIdx, int dx, int dy, int dz) const;
        void RecomputeHidden(u32 brickIdx);
        // Conservative unhide around a GPU edit (edits can open solid rock).
        void InvalidateHiddenNear(const Vec3 &pos, float radius);

        CVulkanContext *m_Context{nullptr};
        Scope<CVoxelSparseBinder> m_Bricks;
        Scope<CBuffer> m_Headers;
        Scope<CBuffer> m_PageTable;
        Scope<CBuffer> m_Palette; // float4 albedo[512]
        Scope<CBuffer> m_EditRing;

        std::vector<VoxelBrickHeader_t> m_HeaderMirror;
        std::vector<i32> m_PageMirror;
        std::vector<VoxelEditCmd_t> m_PendingEdits;
        // Virtual slot per resident brick (for neighbor lookups), opaque-full
        // + hidden flags for interior-brick dispatch culling.
        std::vector<u32> m_BrickSlots;
        std::vector<u8> m_BrickOpaqueFull;
        std::vector<u8> m_BrickHidden;

        u32 m_VirtualDim{64};
        float m_VoxelSize{1.f};
        float m_BrickSize{16.f};
        Vec3 m_WorldMin{0.f};
        u32 m_BrickCount{0}; // high-water allocated slot count
        bool m_bHeadersDirty{false};
    };
} // namespace Manro
