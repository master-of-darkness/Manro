#include "GpuTimer.h"
#include "VulkanContext.h"
#include "VoxelEncoder.h"
#include "VulkanHelpers.h"

#include <stdexcept>

namespace Manro {
    CGpuTimer::CGpuTimer(CVulkanContext &ctx) : m_Context(ctx) {
    }

    CGpuTimer::~CGpuTimer() {
        Shutdown();
    }

    void CGpuTimer::Init(u32 slots, u32 queriesPerSlot) {
        Shutdown();
        if (m_Context.GetTimestampPeriodNs() <= 0.f) return;
        VkQueryPoolCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = slots * queriesPerSlot;
        if (vkCreateQueryPool(m_Context.GetDevice(), &qi, nullptr, &m_Pool) != VK_SUCCESS) {
            m_Pool = VK_NULL_HANDLE;
            return;
        }
        m_Slots = slots;
        m_PerSlot = queriesPerSlot;
        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            vkCmdResetQueryPool(cmd, m_Pool, 0, slots * queriesPerSlot);
        });
    }

    void CGpuTimer::Shutdown() {
        if (m_Pool && m_Context.GetDevice()) {
            vkDestroyQueryPool(m_Context.GetDevice(), m_Pool, nullptr);
            m_Pool = VK_NULL_HANDLE;
        }
        m_Slots = 0;
        m_PerSlot = 0;
    }

    float CGpuTimer::PeriodNs() const {
        return m_Context.GetTimestampPeriodNs();
    }

    void CGpuTimer::Reset(CVoxelEncoder &enc, u32 slot) {
        if (!m_Pool || m_PerSlot == 0) return;
        vkCmdResetQueryPool(enc.NativeCmd(), m_Pool, slot * m_PerSlot, m_PerSlot);
    }

    void CGpuTimer::Stamp(CVoxelEncoder &enc, u32 slot, u32 index, bool top) {
        if (!m_Pool || index >= m_PerSlot) return;
        vkCmdWriteTimestamp(enc.NativeCmd(),
                            top ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            m_Pool, slot * m_PerSlot + index);
    }

    bool CGpuTimer::Poll(u32 slot, u64 *outTicks, u32 count) {
        if (!m_Pool || !outTicks || count == 0) return false;
        return vkGetQueryPoolResults(m_Context.GetDevice(), m_Pool, slot * m_PerSlot, count,
                                     sizeof(u64) * count, outTicks, sizeof(u64),
                                     VK_QUERY_RESULT_64_BIT) == VK_SUCCESS;
    }
}
