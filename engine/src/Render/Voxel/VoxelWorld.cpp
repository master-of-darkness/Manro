#include "VoxelWorld.h"
#include "VoxelSparseBinder.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Buffer.h"
#include "../Vulkan/VulkanHelpers.h"

#include <Manro/Core/Logger.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Manro {
    CVoxelWorld::CVoxelWorld(CVulkanContext &ctx) : m_Context(&ctx) {}

    CVoxelWorld::~CVoxelWorld() { Shutdown(); }

    void CVoxelWorld::Init(const VoxelWorldDesc_t &desc) {
        m_VirtualDim = desc.virtualDim;
        m_VoxelSize = desc.voxelSize;
        m_BrickSize = desc.voxelSize * static_cast<float>(kVoxelBrickEdge);
        m_WorldMin = desc.worldMin;

        const u64 slotCount = static_cast<u64>(m_VirtualDim) * m_VirtualDim * m_VirtualDim;
        const VkDeviceSize brickBytes = static_cast<VkDeviceSize>(kVoxelBrickWords) * sizeof(u32);
        // Virtual size capped by resident budget, not the full virtual grid:
        // the page table sparsely maps slots -> bound pages.
        const VkDeviceSize virtualBytes = static_cast<VkDeviceSize>(desc.maxResidentBricks) * brickBytes;

        m_Bricks = CreateScope<CVoxelSparseBinder>(
            *m_Context, virtualBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

        m_Headers = CreateScope<CBuffer>(
            *m_Context, sizeof(VoxelBrickHeader_t) * desc.maxResidentBricks,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_PageTable = CreateScope<CBuffer>(
            *m_Context, sizeof(i32) * static_cast<size_t>(slotCount > 0 ? slotCount : 1),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_Palette = CreateScope<CBuffer>(
            *m_Context, sizeof(Vec4) * 512,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_EditRing = CreateScope<CBuffer>(
            *m_Context, sizeof(VoxelEditCmd_t) * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_HeaderMirror.assign(desc.maxResidentBricks, VoxelBrickHeader_t{});
        m_PageMirror.assign(static_cast<size_t>(slotCount), -1);
        m_BrickSlots.assign(desc.maxResidentBricks, ~0u);
        m_BrickOpaqueFull.assign(desc.maxResidentBricks, 0u);
        m_BrickHidden.assign(desc.maxResidentBricks, 0u);
        m_BrickCount = 0;
        FlushHeaders();

        // Default palette: flat mid-grey ramp so visibility mode works pre-GI.
        std::vector<Vec4> palette(512, Vec4(0.6f, 0.6f, 0.6f, 1.f));
        m_Palette->LoadData(palette.data(), sizeof(Vec4) * palette.size());
    }

    void CVoxelWorld::Shutdown() {
        m_Bricks.reset();
        m_Headers.reset();
        m_PageTable.reset();
        m_Palette.reset();
        m_EditRing.reset();
        m_HeaderMirror.clear();
        m_PageMirror.clear();
        m_PendingEdits.clear();
        m_BrickSlots.clear();
        m_BrickOpaqueFull.clear();
        m_BrickHidden.clear();
        m_BrickCount = 0;
    }

    i32 CVoxelWorld::AllocateBrick(u32 bx, u32 by, u32 bz) {
        if (bx >= m_VirtualDim || by >= m_VirtualDim || bz >= m_VirtualDim)
            return -1;
        const u32 slot = BrickSlot(bx, by, bz);
        if (m_PageMirror[slot] >= 0)
            return m_PageMirror[slot];
        if (m_BrickCount >= m_HeaderMirror.size()) {
            LOG_ERROR("[CVoxelWorld] Resident brick budget exhausted");
            return -1;
        }
        const VkDeviceSize brickBytes = static_cast<VkDeviceSize>(kVoxelBrickWords) * sizeof(u32);
        if (!m_Bricks->BindRange(static_cast<VkDeviceSize>(m_BrickCount) * brickBytes, brickBytes))
            return -1;

        const u32 brickIdx = m_BrickCount++;
        m_PageMirror[slot] = static_cast<i32>(brickIdx);
        m_BrickSlots[brickIdx] = slot;
        m_BrickOpaqueFull[brickIdx] = 0u;
        m_BrickHidden[brickIdx] = 0u;
        VoxelBrickHeader_t &h = m_HeaderMirror[brickIdx];
        h.origin = m_WorldMin + Vec3(static_cast<float>(bx), static_cast<float>(by),
                                     static_cast<float>(bz)) * m_BrickSize;
        h.pageIndex = brickIdx;
        h.flags = 1u; // resident
        m_bHeadersDirty = true;
        return static_cast<i32>(brickIdx);
    }

    void CVoxelWorld::ReserveResident(u32 brickCount) {
        if (!m_Bricks)
            return;
        const u32 capped = std::min<u32>(brickCount, static_cast<u32>(m_HeaderMirror.size()));
        if (capped == 0)
            return;
        const VkDeviceSize brickBytes = static_cast<VkDeviceSize>(kVoxelBrickWords) * sizeof(u32);
        const VkDeviceSize totalBytes = static_cast<VkDeviceSize>(capped) * brickBytes;
        // One bind for the whole prefix: BindRange batches every touched
        // page into a single vkQueueBindSparse + queue wait.
        if (!m_Bricks->BindRange(0, totalBytes)) {
            LOG_ERROR("[CVoxelWorld] ReserveResident failed for {} bricks", capped);
            return;
        }
        // Fresh sparse pages have UNDEFINED content: zero them so bricks
        // scanned before their fill read as empty (not garbage faces).
        ExecuteOneShot(*m_Context, [&](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, m_Bricks->GetBuffer(), 0, totalBytes, 0);
        });
    }

    namespace {
        // Face-neighbor offsets for hidden-brick recompute + dirty fan-out.
        constexpr int kHiddenNb[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                         {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    }

    void CVoxelWorld::MarkBrickDirty(u32 brickIdx) {
        if (brickIdx >= m_HeaderMirror.size())
            return;
        m_HeaderMirror[brickIdx].flags |= 2u; // task-shader dirty bit
        m_bHeadersDirty = true;
    }

    void CVoxelWorld::MarkBrickAndNeighborsDirty(u32 brickIdx) {
        MarkBrickDirty(brickIdx);
        if (brickIdx >= m_BrickSlots.size())
            return;
        for (const auto &o : kHiddenNb) {
            const i32 n = NeighborBrick(brickIdx, o[0], o[1], o[2]);
            if (n >= 0)
                MarkBrickDirty(static_cast<u32>(n));
        }
    }

    void CVoxelWorld::UploadBrickData(u32 brickIdx, const u16 *mats, const u32 *occupancy) {
        const VkDeviceSize brickBytes = static_cast<VkDeviceSize>(kVoxelBrickWords) * sizeof(u32);
        const VkDeviceSize dstOffset = static_cast<VkDeviceSize>(brickIdx) * brickBytes;

        // Stage via CPU-visible buffer then one-shot device copy (world-gen path only).
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation stagingAlloc = nullptr;
        VmaAllocationInfo stagingInfo{};
        VkBufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = static_cast<VkDeviceSize>(brickBytes);
        ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (vmaCreateBuffer(m_Context->GetAllocator(), &ci, &ai, &staging, &stagingAlloc, &stagingInfo) !=
            VK_SUCCESS) {
            LOG_ERROR("[CVoxelWorld] Staging alloc failed");
            return;
        }
        auto *mapped = static_cast<u8 *>(stagingInfo.pMappedData);
        std::memcpy(mapped, mats, sizeof(u16) * kVoxelBrickVoxels);
        std::memcpy(mapped + sizeof(u16) * kVoxelBrickVoxels, occupancy,
                    sizeof(u32) * kVoxelBrickOccWords);
        vmaFlushAllocation(m_Context->GetAllocator(), stagingAlloc, 0, static_cast<VkDeviceSize>(brickBytes));

        ExecuteOneShot(*m_Context, [&](VkCommandBuffer cmd) {
            VkBufferCopy copy{};
            copy.srcOffset = 0;
            copy.dstOffset = dstOffset;
            copy.size = static_cast<VkDeviceSize>(brickBytes);
            vkCmdCopyBuffer(cmd, staging, m_Bricks->GetBuffer(), 1, &copy);
        });
        vmaDestroyBuffer(m_Context->GetAllocator(), staging, stagingAlloc);
        MarkBrickDirty(brickIdx);
    }

    void CVoxelWorld::UploadBrickBatch(const u32 *indices, const u16 *mats, const u32 *occupancy,
                                       u32 count) {
        if (count == 0 || !indices || !mats || !occupancy)
            return;
        const VkDeviceSize brickBytes = static_cast<VkDeviceSize>(kVoxelBrickWords) * sizeof(u32);
        const VkDeviceSize totalBytes = brickBytes * count;

        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation stagingAlloc = nullptr;
        VmaAllocationInfo stagingInfo{};
        VkBufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = totalBytes;
        ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (vmaCreateBuffer(m_Context->GetAllocator(), &ci, &ai, &staging, &stagingAlloc, &stagingInfo) !=
            VK_SUCCESS) {
            LOG_ERROR("[CVoxelWorld] Batch staging alloc failed ({} bricks)", count);
            return;
        }
        auto *mapped = static_cast<u8 *>(stagingInfo.pMappedData);
        const size_t matBytes = sizeof(u16) * kVoxelBrickVoxels;
        const size_t occBytes = sizeof(u32) * kVoxelBrickOccWords;
        for (u32 i = 0; i < count; ++i) {
            u8 *dst = mapped + static_cast<size_t>(i) * brickBytes;
            std::memcpy(dst, mats + static_cast<size_t>(i) * kVoxelBrickVoxels, matBytes);
            std::memcpy(dst + matBytes, occupancy + static_cast<size_t>(i) * kVoxelBrickOccWords,
                        occBytes);
        }
        vmaFlushAllocation(m_Context->GetAllocator(), stagingAlloc, 0, totalBytes);

        std::vector<VkBufferCopy> copies(count);
        for (u32 i = 0; i < count; ++i) {
            copies[i].srcOffset = static_cast<VkDeviceSize>(i) * brickBytes;
            copies[i].dstOffset = static_cast<VkDeviceSize>(indices[i]) * brickBytes;
            copies[i].size = brickBytes;
        }
        ExecuteOneShot(*m_Context, [&](VkCommandBuffer cmd) {
            vkCmdCopyBuffer(cmd, staging, m_Bricks->GetBuffer(), count, copies.data());
        });
        vmaDestroyBuffer(m_Context->GetAllocator(), staging, stagingAlloc);
        // Invalidate the face cache for the filled bricks (see MarkBrickDirty).
        for (u32 i = 0; i < count; ++i)
            MarkBrickDirty(indices[i]);
    }

    i32 CVoxelWorld::NeighborBrick(u32 brickIdx, int dx, int dy, int dz) const {
        if (brickIdx >= m_BrickSlots.size())
            return -1;
        const u32 slot = m_BrickSlots[brickIdx];
        if (slot == ~0u)
            return -1;
        const i64 bx = static_cast<i64>(slot % m_VirtualDim) + dx;
        const i64 by = static_cast<i64>((slot / m_VirtualDim) % m_VirtualDim) + dy;
        const i64 bz = static_cast<i64>(slot / (static_cast<u64>(m_VirtualDim) * m_VirtualDim)) + dz;
        if (bx < 0 || by < 0 || bz < 0 || bx >= static_cast<i64>(m_VirtualDim) ||
            by >= static_cast<i64>(m_VirtualDim) || bz >= static_cast<i64>(m_VirtualDim))
            return -1;
        const u64 nslot = static_cast<u64>(bx) + static_cast<u64>(by) * m_VirtualDim +
                          static_cast<u64>(bz) * m_VirtualDim * m_VirtualDim;
        if (nslot >= m_PageMirror.size())
            return -1;
        return m_PageMirror[nslot];
    }

    void CVoxelWorld::RecomputeHidden(u32 brickIdx) {
        if (brickIdx >= m_BrickHidden.size())
            return;
        if (m_BrickOpaqueFull[brickIdx] == 0u) {
            m_BrickHidden[brickIdx] = 0u;
            return;
        }
        for (const auto &o : kHiddenNb) {
            const i32 n = NeighborBrick(brickIdx, o[0], o[1], o[2]);
            if (n < 0 || m_BrickOpaqueFull[static_cast<u32>(n)] == 0u) {
                m_BrickHidden[brickIdx] = 0u;
                return;
            }
        }
        m_BrickHidden[brickIdx] = 1u;
    }

    void CVoxelWorld::SetBrickOpaqueFull(u32 brickIdx, bool opaque) {
        if (brickIdx >= m_BrickOpaqueFull.size())
            return;
        m_BrickOpaqueFull[brickIdx] = opaque ? 1u : 0u;
        RecomputeHidden(brickIdx);
        for (const auto &o : kHiddenNb) {
            const i32 n = NeighborBrick(brickIdx, o[0], o[1], o[2]);
            if (n >= 0)
                RecomputeHidden(static_cast<u32>(n));
        }
    }

    void CVoxelWorld::InvalidateHiddenNear(const Vec3 &pos, float radius) {
        if (m_BrickSize <= 0.f || m_BrickHidden.empty())
            return;
        const Vec3 local = (pos - m_WorldMin) / m_BrickSize;
        const int reach = static_cast<int>(std::ceil(radius / m_BrickSize)) + 1;
        const int cx = static_cast<int>(std::floor(local.x));
        const int cy = static_cast<int>(std::floor(local.y));
        const int cz = static_cast<int>(std::floor(local.z));
        const int dim = static_cast<int>(m_VirtualDim);
        for (int dz = -reach; dz <= reach; ++dz) {
            for (int dy = -reach; dy <= reach; ++dy) {
                for (int dx = -reach; dx <= reach; ++dx) {
                    const int bx = cx + dx, by = cy + dy, bz = cz + dz;
                    if (bx < 0 || by < 0 || bz < 0 || bx >= dim || by >= dim || bz >= dim)
                        continue;
                    const size_t slot = static_cast<size_t>(bx) + static_cast<size_t>(by) * dim +
                                        static_cast<size_t>(bz) * dim * dim;
                    if (slot >= m_PageMirror.size())
                        continue;
                    const i32 b = m_PageMirror[slot];
                    if (b >= 0) {
                        m_BrickHidden[static_cast<u32>(b)] = 0u;
                        // An erase can reveal previously culled boundary faces
                        // on adjacent bricks: force their face caches to
                        // re-evaluate (cross-brick culling correctness).
                        MarkBrickAndNeighborsDirty(static_cast<u32>(b));
                    }
                }
            }
        }
    }
    void CVoxelWorld::SetVoxel(u32 bx, u32 by, u32 bz, u32 lx, u32 ly, u32 lz, u16 mat) {
        i32 brick = AllocateBrick(bx, by, bz);
        if (brick < 0)
            return;
        // Read-modify-write via staging copy of the single brick (gen-time path).
        // Gameplay edits go through QueueEdit() -> GPU compute instead.
        const VkDeviceSize brickBytes = static_cast<VkDeviceSize>(kVoxelBrickWords) * sizeof(u32);
        const VkDeviceSize srcOffset = static_cast<VkDeviceSize>(brick) * brickBytes;
        std::vector<u8> scratch(static_cast<size_t>(brickBytes));
        // NOTE: device-local sparse pages are not host-visible; world-gen should
        // batch via UploadBrickData. This path stages through a temp device copy.
        (void)scratch;
        (void)srcOffset;
        (void)lx; (void)ly; (void)lz; (void)mat;
        LOG_WARN("[CVoxelWorld] SetVoxel single-write: batch via UploadBrickData instead");
    }

    void CVoxelWorld::QueueEdit(const VoxelEditCmd_t &cmd) {
        if (m_PendingEdits.size() >= 1024) {
            LOG_WARN("[CVoxelWorld] Edit ring full, dropping edit");
            return;
        }
        // An edit can open a hole into (or out of) solid rock: conservatively
        // unhide the affected neighborhood so hidden-brick culling can never
        // hide fresh geometry. Re-hiding happens if the world source refills.
        InvalidateHiddenNear(cmd.pos, cmd.radius);
        m_PendingEdits.push_back(cmd);
    }

    void CVoxelWorld::ClearEdits() { m_PendingEdits.clear(); }

    VkDeviceAddress CVoxelWorld::GetBrickBufferAddr() const {
        return m_Bricks ? m_Bricks->GetDeviceAddress() : 0;
    }

    VkDeviceAddress CVoxelWorld::GetHeaderAddr() const {
        return m_Headers ? m_Headers->GetDeviceAddress() : 0;
    }

    VkDeviceAddress CVoxelWorld::GetPageTableAddr() const {
        return m_PageTable ? m_PageTable->GetDeviceAddress() : 0;
    }

    void CVoxelWorld::FlushHeaders() {
        if (!m_Headers || !m_PageTable)
            return;
        // Headers/page-table are host-written into buffers that in-flight
        // frames are reading: re-upload only when flagged dirty (allocation
        // or CPU fills). Edit frames only need the edit ring — re-uploading
        // headers there would needlessly rescan bricks whose mirror dirty
        // bits are stale (the GPU already consumed them).
        if (m_bHeadersDirty) {
            m_Headers->LoadData(m_HeaderMirror.data(), sizeof(VoxelBrickHeader_t) * m_HeaderMirror.size());
            m_PageTable->LoadData(m_PageMirror.data(), sizeof(i32) * m_PageMirror.size());
            // Consume the mirror dirty bits: the GPU copy now carries them
            // and the task shader clears them after rescanning. Keeping them
            // set would rescan every filled brick on every later upload.
            for (auto &h : m_HeaderMirror)
                h.flags &= ~2u;
            m_bHeadersDirty = false;
        }
        if (!m_PendingEdits.empty())
            m_EditRing->LoadData(m_PendingEdits.data(), sizeof(VoxelEditCmd_t) * m_PendingEdits.size());
    }
} // namespace Manro
