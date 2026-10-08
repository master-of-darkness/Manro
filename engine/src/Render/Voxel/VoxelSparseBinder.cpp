#include "VoxelSparseBinder.h"
#include "../Vulkan/VulkanContext.h"

#include <Manro/Core/Logger.h>
#include <stdexcept>

namespace Manro {
    CVoxelSparseBinder::CVoxelSparseBinder(CVulkanContext &ctx, VkDeviceSize virtualSizeBytes,
                                           VkBufferUsageFlags usage)
        : m_Context(ctx), m_VirtualSize(virtualSizeBytes) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(ctx.GetPhysicalDevice(), &memProps);

        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = virtualSizeBytes;
        bi.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bi.flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT;

        if (vkCreateBuffer(ctx.GetDevice(), &bi, nullptr, &m_Buffer) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelSparseBinder] Failed to create sparse buffer");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(ctx.GetDevice(), m_Buffer, &req);
        m_PageSize = req.alignment;

        m_MemoryTypeIndex = UINT32_MAX;
        for (u32 i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((req.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                m_MemoryTypeIndex = i;
                break;
            }
        }
        if (m_MemoryTypeIndex == UINT32_MAX) {
            for (u32 i = 0; i < memProps.memoryTypeCount; ++i) {
                if (req.memoryTypeBits & (1u << i)) {
                    m_MemoryTypeIndex = i;
                    break;
                }
            }
        }
        if (m_MemoryTypeIndex == UINT32_MAX) {
            vkDestroyBuffer(ctx.GetDevice(), m_Buffer, nullptr);
            throw std::runtime_error("[CVoxelSparseBinder] No suitable sparse memory type");
        }

        const u32 pageCount = static_cast<u32>((virtualSizeBytes + m_PageSize - 1) / m_PageSize);
        m_Pages.resize(pageCount);
    }

    CVoxelSparseBinder::~CVoxelSparseBinder() {
        VkDevice device = m_Context.GetDevice();
        for (auto &p : m_Pages) {
            if (p.memory) {
                vkFreeMemory(device, p.memory, nullptr);
                p.memory = VK_NULL_HANDLE;
            }
        }
        if (m_Buffer) {
            vkDestroyBuffer(device, m_Buffer, nullptr);
            m_Buffer = VK_NULL_HANDLE;
        }
    }

    bool CVoxelSparseBinder::BindRange(VkDeviceSize offset, VkDeviceSize size) {
        if (offset + size > m_VirtualSize) {
            LOG_ERROR("[CVoxelSparseBinder] BindRange out of virtual range");
            return false;
        }
        VkDevice device = m_Context.GetDevice();
        const u32 firstPage = static_cast<u32>(offset / m_PageSize);
        const u32 lastPage = static_cast<u32>((offset + size - 1) / m_PageSize);

        std::vector<VkSparseMemoryBind> binds;
        std::vector<VkDeviceMemory> freshAllocs;
        binds.reserve(lastPage - firstPage + 1);

        for (u32 page = firstPage; page <= lastPage; ++page) {
            if (m_Pages[page].memory)
                continue;
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = m_PageSize;
            ai.memoryTypeIndex = m_MemoryTypeIndex;

            VkMemoryAllocateFlagsInfo flags{};
            flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
            flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            ai.pNext = &flags;
            VkDeviceMemory mem = VK_NULL_HANDLE;
            if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS) {
                for (VkDeviceMemory m : freshAllocs)
                    vkFreeMemory(device, m, nullptr);
                LOG_ERROR("[CVoxelSparseBinder] Sparse page alloc failed (page {})", page);
                return false;
            }
            freshAllocs.push_back(mem);
            VkSparseMemoryBind b{};
            b.resourceOffset = static_cast<VkDeviceSize>(page) * m_PageSize;
            b.size = m_PageSize;
            b.memory = mem;
            b.memoryOffset = 0;
            b.flags = 0;
            binds.push_back(b);
        }

        if (!binds.empty()) {
            VkSparseBufferMemoryBindInfo bufBind{};
            bufBind.buffer = m_Buffer;
            bufBind.bindCount = static_cast<u32>(binds.size());
            bufBind.pBinds = binds.data();

            VkBindSparseInfo bindInfo{};
            bindInfo.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
            bindInfo.bufferBindCount = 1;
            bindInfo.pBufferBinds = &bufBind;

            if (vkQueueBindSparse(m_Context.GetGraphicsQueue(), 1, &bindInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
                for (VkDeviceMemory m : freshAllocs)
                    vkFreeMemory(device, m, nullptr);
                LOG_ERROR("[CVoxelSparseBinder] vkQueueBindSparse failed");
                return false;
            }
            vkQueueWaitIdle(m_Context.GetGraphicsQueue());

            size_t fresh = 0;
            for (u32 page = firstPage; page <= lastPage; ++page) {
                if (m_Pages[page].memory)
                    continue;
                m_Pages[page].memory = freshAllocs[fresh++];
                m_Pages[page].offset = static_cast<VkDeviceSize>(page) * m_PageSize;
                ++m_BoundPages;
            }
        }
        return true;
    }

    void CVoxelSparseBinder::UnbindRange(VkDeviceSize offset, VkDeviceSize size) {
        if (offset + size > m_VirtualSize)
            return;
        VkDevice device = m_Context.GetDevice();
        const u32 firstPage = static_cast<u32>(offset / m_PageSize);
        const u32 lastPage = static_cast<u32>((offset + size - 1) / m_PageSize);

        std::vector<VkSparseMemoryBind> binds;
        for (u32 page = firstPage; page <= lastPage; ++page) {
            if (!m_Pages[page].memory)
                continue;
            VkSparseMemoryBind b{};
            b.resourceOffset = static_cast<VkDeviceSize>(page) * m_PageSize;
            b.size = m_PageSize;
            b.memory = VK_NULL_HANDLE;
            binds.push_back(b);
        }
        if (!binds.empty()) {
            VkSparseBufferMemoryBindInfo bufBind{};
            bufBind.buffer = m_Buffer;
            bufBind.bindCount = static_cast<u32>(binds.size());
            bufBind.pBinds = binds.data();
            VkBindSparseInfo bindInfo{};
            bindInfo.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
            bindInfo.bufferBindCount = 1;
            bindInfo.pBufferBinds = &bufBind;
            if (vkQueueBindSparse(m_Context.GetGraphicsQueue(), 1, &bindInfo, VK_NULL_HANDLE) == VK_SUCCESS)
                vkQueueWaitIdle(m_Context.GetGraphicsQueue());
        }
        for (u32 page = firstPage; page <= lastPage; ++page) {
            if (m_Pages[page].memory) {
                vkFreeMemory(device, m_Pages[page].memory, nullptr);
                m_Pages[page].memory = VK_NULL_HANDLE;
                --m_BoundPages;
            }
        }
    }

    VkDeviceAddress CVoxelSparseBinder::GetDeviceAddress() const {
        VkBufferDeviceAddressInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = m_Buffer;
        return vkGetBufferDeviceAddress(m_Context.GetDevice(), &info);
    }
}
