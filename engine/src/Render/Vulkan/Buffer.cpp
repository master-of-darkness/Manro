#include "Buffer.h"
#include "VulkanContext.h"

#include <volk.h>
#include <stdexcept>
#include <cstring>

namespace Manro {
    static VkBufferUsageFlags ToNativeUsage(EBufferUsage usage) {
        VkBufferUsageFlags out = 0;
        if (HasUsage(usage, EBufferUsage::Storage)) out |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (HasUsage(usage, EBufferUsage::TransferSrc)) out |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (HasUsage(usage, EBufferUsage::TransferDst)) out |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (HasUsage(usage, EBufferUsage::ShaderAddress))
            out |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        return out;
    }

    static VmaMemoryUsage ToNativeMemory(EBufferMemory memory) {
        return memory == EBufferMemory::GpuOnly ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU;
    }

    CBuffer::CBuffer(const CVulkanContext &context, u64 sizeBytes, EBufferUsage usage, EBufferMemory memory)
        : CBuffer(context, static_cast<VkDeviceSize>(sizeBytes), ToNativeUsage(usage),
                  ToNativeMemory(memory)) {
    }

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

    void CBuffer::FlushRange(const size_t offset, const size_t size) const {
        vmaFlushAllocation(m_Context.GetAllocator(), m_Allocation, offset, size);
    }

    VkDeviceAddress CBuffer::GetDeviceAddress() const {
        VkBufferDeviceAddressInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = m_Buffer;
        return vkGetBufferDeviceAddress(m_Context.GetDevice(), &info);
    }

    u64 CBuffer::Address() const {
        return static_cast<u64>(GetDeviceAddress());
    }

    u64 CBuffer::SizeBytes() const {
        return static_cast<u64>(m_unSize);
    }

    void CBuffer::Flush(u64 offset, u64 size) const {
        FlushRange(static_cast<size_t>(offset), static_cast<size_t>(size));
    }

    void CBuffer::Invalidate(u64 offset, u64 size) const {
        vmaInvalidateAllocation(m_Context.GetAllocator(), m_Allocation, static_cast<VkDeviceSize>(offset),
                                static_cast<VkDeviceSize>(size));
    }
} // namespace Manro
