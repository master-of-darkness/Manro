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

        // Fill brick voxel data (CPU staging -> device copy via one-shot).
        // mats: 4096 uint16, occ edited via SetVoxel or full upload.
        void UploadBrickData(u32 brickIdx, const u16 *mats, const u32 *occupancy);

        // Single-voxel CPU write (staging path for world-gen; gameplay uses GPU edits).
        void SetVoxel(u32 bx, u32 by, u32 bz, u32 lx, u32 ly, u32 lz, u16 mat);

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

        CVulkanContext *m_Context{nullptr};
        Scope<CVoxelSparseBinder> m_Bricks;
        Scope<CBuffer> m_Headers;
        Scope<CBuffer> m_PageTable;
        Scope<CBuffer> m_Palette; // float4 albedo[512]
        Scope<CBuffer> m_EditRing;

        std::vector<VoxelBrickHeader_t> m_HeaderMirror;
        std::vector<i32> m_PageMirror;
        std::vector<VoxelEditCmd_t> m_PendingEdits;

        u32 m_VirtualDim{64};
        float m_VoxelSize{1.f};
        float m_BrickSize{16.f};
        Vec3 m_WorldMin{0.f};
        u32 m_BrickCount{0}; // high-water allocated slot count
        bool m_bHeadersDirty{false};
    };
} // namespace Manro
