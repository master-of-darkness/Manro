#pragma once

#include "GpuDefs.h"
#include <volk.h>
#include <vector>

namespace Manro {
    class CVulkanContext;

    class CSparseBuffer {
    public:
        CSparseBuffer(CVulkanContext &ctx, u64 sizeBytes, EBufferUsage usage);

        ~CSparseBuffer();

        CSparseBuffer(const CSparseBuffer &) = delete;
        CSparseBuffer &operator=(const CSparseBuffer &) = delete;

        bool BindRange(u64 offset, u64 size);
        void UnbindRange(u64 offset, u64 size);

        [[nodiscard]] u64 Address() const;
        [[nodiscard]] u64 Size() const { return m_VirtualSize; }
        [[nodiscard]] u64 PageSize() const { return m_PageSize; }
        [[nodiscard]] u32 BoundPages() const { return m_BoundPages; }
        [[nodiscard]] u64 TakeBinds() {
            const u64 n = m_BindSubmitCount;
            m_BindSubmitCount = 0;
            return n;
        }

        [[nodiscard]] VkBuffer NativeBuffer() const { return m_Buffer; }

    private:
        struct PageEntry {
            VkDeviceMemory memory{VK_NULL_HANDLE};
            u64 offset{0};
        };

        CVulkanContext &m_Context;
        VkBuffer m_Buffer{VK_NULL_HANDLE};
        u64 m_VirtualSize{0};
        u64 m_PageSize{65536};
        u32 m_BoundPages{0};
        u32 m_MemoryTypeIndex{0};
        u64 m_BindSubmitCount{0};
        std::vector<PageEntry> m_Pages;
    };
}
