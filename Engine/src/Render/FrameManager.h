#pragma once

#include <volk.h>
#include <Core/Types.h>
#include <vector>

namespace Engine {
    class VulkanContext;

    struct FrameData {
        VkCommandPool commandPool{VK_NULL_HANDLE};
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkSemaphore imageAvailableSemaphore{VK_NULL_HANDLE};
        VkSemaphore renderFinishedSemaphore{VK_NULL_HANDLE};
        VkFence inFlightFence{VK_NULL_HANDLE};
    };

    class FrameManager {
    public:
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

        void Initialize(const VulkanContext& ctx);
        void Shutdown(VkDevice device);

        FrameData& CurrentFrame() { return m_Frames[m_CurrentFrame]; }
        const FrameData& CurrentFrame() const { return m_Frames[m_CurrentFrame]; }
        u32 CurrentFrameIndex() const { return m_CurrentFrame; }

        void AdvanceFrame() { m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT; }

    private:
        void CreateCommandBuffers(VkDevice device, u32 queueFamilyIndex);
        void CreateSyncObjects(VkDevice device);

        std::vector<FrameData> m_Frames;
        u32 m_CurrentFrame{0};
    };
} // namespace Engine
