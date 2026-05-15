#include "Buffer.h"
#include "VulkanContext.h"

#include <volk.h>
#include <stdexcept>
#include <cstring>

namespace Manro {
    CBuffer::CBuffer(const CVulkanContext &context, VkDeviceSize size, VkBufferUsageFlags usage,
                     VmaMemoryUsage memoryUsage)
        : m_Context(context), m_unSize(size) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memoryUsage;
        if (memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY || memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU) {
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        if (vmaCreateBuffer(m_Context.GetAllocator(), &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation,
                            &m_AllocationInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer!");
        }
    }

    CBuffer::~CBuffer() {
        if (m_Buffer) {
            vmaDestroyBuffer(m_Context.GetAllocator(), m_Buffer, m_Allocation);
        }
    }

    void *CBuffer::Map() const {
        void *data;
        vmaMapMemory(m_Context.GetAllocator(), m_Allocation, &data);
        return data;
    }

    void CBuffer::Unmap() const {
        vmaUnmapMemory(m_Context.GetAllocator(), m_Allocation);
    }

    void CBuffer::LoadData(const void *data, const size_t size, const size_t offset) const {
        void *mappedData;

        if (m_AllocationInfo.pMappedData) {
            mappedData = m_AllocationInfo.pMappedData;
        } else {
            mappedData = Map();
        }

        std::memcpy(static_cast<char *>(mappedData) + offset, data, size);

        vmaFlushAllocation(m_Context.GetAllocator(), m_Allocation, offset, size);

        if (!m_AllocationInfo.pMappedData) {
            Unmap();
        }
    }
} // namespace Manro
