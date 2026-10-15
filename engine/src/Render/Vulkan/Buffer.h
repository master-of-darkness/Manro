#pragma once

#include "GpuDefs.h"
#include <vk_mem_alloc.h>

namespace Manro {
    class CVulkanContext;

    class CBuffer {
    public:
        CBuffer(const CVulkanContext &context, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

        CBuffer(const CVulkanContext &context, u64 sizeBytes, EBufferUsage usage, EBufferMemory memory);

        ~CBuffer();

        void *Map() const;

        void Unmap() const;

        void LoadData(const void *data, size_t size, size_t offset = 0) const;

        [[nodiscard]] VkDeviceAddress GetDeviceAddress() const;

        [[nodiscard]] u64 Address() const;

        [[nodiscard]] u64 SizeBytes() const;

        // DEBUG/diagnostic access (host-visible buffers only).
        [[nodiscard]] VmaAllocation GetAllocation() const { return m_Allocation; }
        [[nodiscard]] const void *GetMapped() const { return m_AllocationInfo.pMappedData; }

        [[nodiscard]] void *GetMappedMutable() const { return m_AllocationInfo.pMappedData; }
        void FlushRange(size_t offset, size_t size) const;
        void Flush(u64 offset, u64 size) const;
        void Invalidate(u64 offset, u64 size) const;

        VkBuffer GetHandle() const { return m_Buffer; }

        VkDeviceSize GetSize() const { return m_unSize; }

    private:
        const CVulkanContext &m_Context;
        VkBuffer m_Buffer{nullptr};
        VmaAllocation m_Allocation{nullptr};
        VmaAllocationInfo m_AllocationInfo{};
        VkDeviceSize m_unSize{0};
    };
} // namespace Manro
