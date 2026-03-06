#include "FrameManager.h"
#include "VulkanContext.h"
#include <stdexcept>

namespace Engine {
    void FrameManager::Initialize(const VulkanContext& ctx) {
        CreateCommandBuffers(ctx.GetDevice(), ctx.GetGraphicsQueueFamilyIndex());
        CreateSyncObjects(ctx.GetDevice());
    }

    void FrameManager::Shutdown(VkDevice device) {
        for (auto& frame : m_Frames) {
            if (frame.renderFinishedSemaphore)
                vkDestroySemaphore(device, frame.renderFinishedSemaphore, nullptr);
            if (frame.imageAvailableSemaphore)
                vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr);
            if (frame.inFlightFence)
                vkDestroyFence(device, frame.inFlightFence, nullptr);
            if (frame.commandPool)
                vkDestroyCommandPool(device, frame.commandPool, nullptr);
        }
        m_Frames.clear();
    }

    void FrameManager::CreateCommandBuffers(VkDevice device, u32 queueFamilyIndex) {
        m_Frames.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndex;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_Frames[i].commandPool) != VK_SUCCESS)
                throw std::runtime_error("Failed to create command pool!");

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = m_Frames[i].commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            if (vkAllocateCommandBuffers(device, &allocInfo, &m_Frames[i].commandBuffer) != VK_SUCCESS)
                throw std::runtime_error("Failed to allocate command buffers!");
        }
    }

    void FrameManager::CreateSyncObjects(VkDevice device) {
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateSemaphore(device, &semInfo, nullptr, &m_Frames[i].imageAvailableSemaphore) !=
                VK_SUCCESS ||
                vkCreateSemaphore(device, &semInfo, nullptr, &m_Frames[i].renderFinishedSemaphore) !=
                VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, nullptr, &m_Frames[i].inFlightFence) != VK_SUCCESS)
                throw std::runtime_error("Failed to create sync objects!");
        }
    }
} // namespace Engine
