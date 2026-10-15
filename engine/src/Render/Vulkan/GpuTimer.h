#pragma once

#include "GpuDefs.h"
#include <volk.h>

namespace Manro {
    class CVulkanContext;
    class CVoxelEncoder;

    class CGpuTimer {
    public:
        explicit CGpuTimer(CVulkanContext &ctx);
        ~CGpuTimer();

        CGpuTimer(const CGpuTimer &) = delete;
        CGpuTimer &operator=(const CGpuTimer &) = delete;

        void Init(u32 slots, u32 queriesPerSlot);
        void Shutdown();
        bool Valid() const { return m_Pool != VK_NULL_HANDLE; }
        float PeriodNs() const;
        u32 Slots() const { return m_Slots; }
        u32 QueriesPerSlot() const { return m_PerSlot; }
        void Reset(CVoxelEncoder &enc, u32 slot);
        void Stamp(CVoxelEncoder &enc, u32 slot, u32 index, bool top);
        bool Poll(u32 slot, u64 *outTicks, u32 count);

    private:
        CVulkanContext &m_Context;
        VkQueryPool m_Pool{VK_NULL_HANDLE};
        u32 m_Slots{0};
        u32 m_PerSlot{0};
    };
}
