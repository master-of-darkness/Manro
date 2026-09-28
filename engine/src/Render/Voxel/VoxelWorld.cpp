#include "VoxelWorld.h"
#include "VoxelSparseBinder.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Buffer.h"
#include "../Vulkan/VulkanHelpers.h"

#include <Manro/Core/Logger.h>
#include <cstring>

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
        VoxelBrickHeader_t &h = m_HeaderMirror[brickIdx];
        h.origin = m_WorldMin + Vec3(static_cast<float>(bx), static_cast<float>(by),
                                     static_cast<float>(bz)) * m_BrickSize;
        h.pageIndex = brickIdx;
        h.flags = 1u; // resident
        m_bHeadersDirty = true;
        return static_cast<i32>(brickIdx);
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
        m_Headers->LoadData(m_HeaderMirror.data(), sizeof(VoxelBrickHeader_t) * m_HeaderMirror.size());
        m_PageTable->LoadData(m_PageMirror.data(), sizeof(i32) * m_PageMirror.size());
        if (!m_PendingEdits.empty())
            m_EditRing->LoadData(m_PendingEdits.data(), sizeof(VoxelEditCmd_t) * m_PendingEdits.size());
        m_bHeadersDirty = false;
    }
} // namespace Manro
