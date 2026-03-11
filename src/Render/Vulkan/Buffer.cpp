#include <volk.h>

#include <Manro/Render/Vulkan/Buffer.h>
#include <Manro/Render/Vulkan/VulkanContext.h>
#include <stdexcept>
#include <cstring>

namespace Engine {
    Buffer::Buffer(const VulkanContext &context, VkDeviceSize size, VkBufferUsageFlags usage,
                   VmaMemoryUsage memoryUsage)
        : m_Context(context), m_Size(size) {
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

    Buffer::~Buffer() {
        if (m_Buffer) {
            vmaDestroyBuffer(m_Context.GetAllocator(), m_Buffer, m_Allocation);
        }
    }

    void *Buffer::Map() {
        void *data;
        vmaMapMemory(m_Context.GetAllocator(), m_Allocation, &data);
        return data;
    }

    void Buffer::Unmap() {
        vmaUnmapMemory(m_Context.GetAllocator(), m_Allocation);
    }

    void Buffer::LoadData(const void *data, size_t size, size_t offset) {
        void *mappedData;

        if (m_AllocationInfo.pMappedData) {
            mappedData = m_AllocationInfo.pMappedData;
        } else {
            mappedData = Map();
        }

        std::memcpy(static_cast<char *>(mappedData) + offset, data, size);

        if (!m_AllocationInfo.pMappedData) {
            Unmap();
        }
    }

    VkDeviceAddress Buffer::GetDeviceAddress() const {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = m_Buffer;
        return vkGetBufferDeviceAddress(m_Context.GetDevice(), &addressInfo);
    }
} // namespace Engine
