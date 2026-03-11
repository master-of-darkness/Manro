#pragma once
#include <Core/Types.h>
#include <vk_mem_alloc.h>

namespace Engine {
    class VulkanContext;

    class Buffer {
    public:
        Buffer(const VulkanContext &context, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

        ~Buffer();

        void *Map();

        void Unmap();

        void LoadData(const void *data, size_t size, size_t offset = 0);

        VkBuffer GetHandle() const { return m_Buffer; }
        VkDeviceSize GetSize() const { return m_Size; }

        VkDeviceAddress GetDeviceAddress() const;

    private:
        const VulkanContext &m_Context;
        VkBuffer m_Buffer{nullptr};
        VmaAllocation m_Allocation{nullptr};
        VmaAllocationInfo m_AllocationInfo{};
        VkDeviceSize m_Size{0};
    };
} // namespace Engine