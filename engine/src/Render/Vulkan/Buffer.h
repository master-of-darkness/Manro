#pragma once

#include <vk_mem_alloc.h>

namespace Manro {
    class CVulkanContext;

    class CBuffer {
    public:
        CBuffer(const CVulkanContext &context, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

        ~CBuffer();

        void *Map() const;

        void Unmap() const;

        void LoadData(const void *data, size_t size, size_t offset = 0) const;

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
