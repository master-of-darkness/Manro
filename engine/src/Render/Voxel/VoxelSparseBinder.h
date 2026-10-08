#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>
#include <vector>

namespace Manro {
    class CVulkanContext;

    class CVoxelSparseBinder {
    public:
        CVoxelSparseBinder(CVulkanContext &ctx, VkDeviceSize virtualSizeBytes,
                           VkBufferUsageFlags usage);

        ~CVoxelSparseBinder();

        CVoxelSparseBinder(const CVoxelSparseBinder &) = delete;
        CVoxelSparseBinder &operator=(const CVoxelSparseBinder &) = delete;

        bool BindRange(VkDeviceSize offset, VkDeviceSize size);

        void UnbindRange(VkDeviceSize offset, VkDeviceSize size);

        [[nodiscard]] VkBuffer GetBuffer() const { return m_Buffer; }
        [[nodiscard]] VkDeviceAddress GetDeviceAddress() const;
        [[nodiscard]] VkDeviceSize GetPageSize() const { return m_PageSize; }
        [[nodiscard]] VkDeviceSize GetVirtualSize() const { return m_VirtualSize; }
        [[nodiscard]] u32 GetBoundPageCount() const { return m_BoundPages; }

    private:
        struct BoundPage_t {
            VkDeviceMemory memory{VK_NULL_HANDLE};
            VkDeviceSize offset{0};
        };

        CVulkanContext &m_Context;
        VkBuffer m_Buffer{VK_NULL_HANDLE};
        VkDeviceSize m_VirtualSize{0};
        VkDeviceSize m_PageSize{65536};
        u32 m_BoundPages{0};
        u32 m_MemoryTypeIndex{0};
        std::vector<BoundPage_t> m_Pages;
    };
}
