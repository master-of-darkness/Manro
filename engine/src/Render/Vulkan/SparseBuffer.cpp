#include "SparseBuffer.h"
#include "VulkanContext.h"

#include <Manro/Core/Logger.h>
#include <stdexcept>

namespace Manro {
    static VkBufferUsageFlags SparseNativeUsage(EBufferUsage usage) {
        VkBufferUsageFlags out = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if (HasUsage(usage, EBufferUsage::Storage)) out |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (HasUsage(usage, EBufferUsage::TransferSrc)) out |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (HasUsage(usage, EBufferUsage::TransferDst)) out |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        return out;
    }

    CSparseBuffer::CSparseBuffer(CVulkanContext &ctx, u64 sizeBytes, EBufferUsage usage)
        : m_Context(ctx), m_VirtualSize(sizeBytes) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(ctx.GetPhysicalDevice(), &memProps);

        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = static_cast<VkDeviceSize>(sizeBytes);
        bi.usage = SparseNativeUsage(usage);
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bi.flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT;

        if (vkCreateBuffer(ctx.GetDevice(), &bi, nullptr, &m_Buffer) != VK_SUCCESS)
            throw std::runtime_error("[CSparseBuffer] Failed to create sparse buffer");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(ctx.GetDevice(), m_Buffer, &req);
        m_PageSize = static_cast<u64>(req.alignment);

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
            throw std::runtime_error("[CSparseBuffer] No suitable sparse memory type");
        }

        const u32 pageCount = static_cast<u32>((sizeBytes + m_PageSize - 1) / m_PageSize);
        m_Pages.resize(pageCount);
    }

    CSparseBuffer::~CSparseBuffer() {
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

    bool CSparseBuffer::BindRange(u64 offset, u64 size) {
        if (offset + size > m_VirtualSize) {
            LOG_ERROR("[CSparseBuffer] BindRange out of virtual range");
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
            ai.allocationSize = static_cast<VkDeviceSize>(m_PageSize);
            ai.memoryTypeIndex = m_MemoryTypeIndex;

            VkMemoryAllocateFlagsInfo flags{};
            flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
            flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            ai.pNext = &flags;
            VkDeviceMemory mem = VK_NULL_HANDLE;
            if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS) {
                for (VkDeviceMemory m : freshAllocs)
                    vkFreeMemory(device, m, nullptr);
                LOG_ERROR("[CSparseBuffer] Sparse page alloc failed (page {})", page);
                return false;
            }
            freshAllocs.push_back(mem);
            VkSparseMemoryBind b{};
            b.resourceOffset = static_cast<VkDeviceSize>(page) * static_cast<VkDeviceSize>(m_PageSize);
            b.size = static_cast<VkDeviceSize>(m_PageSize);
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

            VkFence fence{VK_NULL_HANDLE};
            VkFenceCreateInfo fci{};
            fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            bool ok = vkCreateFence(device, &fci, nullptr, &fence) == VK_SUCCESS;
            if (ok) {
                ok = vkQueueBindSparse(m_Context.GetGraphicsQueue(), 1, &bindInfo, fence) ==
                     VK_SUCCESS;
            }
            if (ok) {
                ok = vkWaitForFences(device, 1, &fence, VK_TRUE, 1000000000ull) == VK_SUCCESS;
            }
            if (fence)
                vkDestroyFence(device, fence, nullptr);
            if (!ok) {
                for (VkDeviceMemory m : freshAllocs)
                    vkFreeMemory(device, m, nullptr);
                LOG_ERROR("[CSparseBuffer] sparse bind failed/timed out");
                return false;
            }
            ++m_BindSubmitCount;

            size_t fresh = 0;
            for (u32 page = firstPage; page <= lastPage; ++page) {
                if (m_Pages[page].memory)
                    continue;
                m_Pages[page].memory = freshAllocs[fresh++];
                m_Pages[page].offset = static_cast<u64>(page) * m_PageSize;
                ++m_BoundPages;
            }
        }
        return true;
    }

    void CSparseBuffer::UnbindRange(u64 offset, u64 size) {
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
            b.resourceOffset = static_cast<VkDeviceSize>(page) * static_cast<VkDeviceSize>(m_PageSize);
            b.size = static_cast<VkDeviceSize>(m_PageSize);
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
            VkFence fence{VK_NULL_HANDLE};
            VkFenceCreateInfo fci{};
            fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            bool ok = vkCreateFence(device, &fci, nullptr, &fence) == VK_SUCCESS;
            if (ok) {
                ok = vkQueueBindSparse(m_Context.GetGraphicsQueue(), 1, &bindInfo, fence) ==
                     VK_SUCCESS;
            }
            if (ok) {
                ok = vkWaitForFences(device, 1, &fence, VK_TRUE, 1000000000ull) == VK_SUCCESS;
            }
            if (fence)
                vkDestroyFence(device, fence, nullptr);
            if (ok)
                ++m_BindSubmitCount;
            else
                LOG_ERROR("[CSparseBuffer] sparse unbind failed/timed out");
        }
        for (u32 page = firstPage; page <= lastPage; ++page) {
            if (m_Pages[page].memory) {
                vkFreeMemory(device, m_Pages[page].memory, nullptr);
                m_Pages[page].memory = VK_NULL_HANDLE;
                --m_BoundPages;
            }
        }
    }

    u64 CSparseBuffer::Address() const {
        VkBufferDeviceAddressInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = m_Buffer;
        return static_cast<u64>(vkGetBufferDeviceAddress(m_Context.GetDevice(), &info));
    }
}
