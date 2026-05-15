#pragma once

#include "VkBootstrap.h"
#include "volk.h"

#include <vk_mem_alloc.h>
#include <Manro/Core/Types.h>

namespace Manro {
    class IWindow;

    class CVulkanContext {
    public:
        CVulkanContext(const char *appName, const IWindow &window);

        ~CVulkanContext();

        CVulkanContext(const CVulkanContext &) = delete;

        CVulkanContext &operator=(const CVulkanContext &) = delete;

        VkInstance GetInstance() const { return m_Instance; }

        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }

        VkDevice GetDevice() const { return m_Device; }

        VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }

        u32 GetGraphicsQueueFamilyIndex() const { return m_unGraphicsQueueFamilyIndex; }

        VkCommandPool GetOneShotCommandPool() const;

        VkCommandBuffer GetOneShotCommandBuffer() const;

        VkFence GetOneShotFence() const;

        VmaAllocator GetAllocator() const { return m_Allocator; }

        VkSurfaceKHR GetSurface() const { return m_Surface; }

        VkSampleCountFlagBits GetMaxUsableSampleCount() const;

        void GetVramStats(u64 &usage, u64 &budget) const;

        u64 GetTimelineSemaphoreCounterValue(VkSemaphore semaphore) const {
            u64 value = 0;
            vkGetSemaphoreCounterValue(m_Device, semaphore, &value);
            return value;
        }

    private:
        void CreateInstance(const char *appName);

        void CreateSurface(const IWindow &window);

        void PickPhysicalDevice();

        void CreateLogicalDevice();

        void EnsureOneShotResources() const;

        VkInstance m_Instance{nullptr};
        VkPhysicalDevice m_PhysicalDevice{nullptr};
        VkDevice m_Device{nullptr};

        vkb::Instance vkb_Instance;
        vkb::Device vkb_Device;
        vkb::PhysicalDevice vkb_PhysDev;
        VkQueue m_GraphicsQueue{nullptr};
        u32 m_unGraphicsQueueFamilyIndex{0};
        VkSurfaceKHR m_Surface{nullptr};
        VmaAllocator m_Allocator{nullptr};
        mutable VkCommandPool m_OneShotCommandPool{VK_NULL_HANDLE};
        mutable VkCommandBuffer m_OneShotCommandBuffer{VK_NULL_HANDLE};
        mutable VkFence m_OneShotFence{VK_NULL_HANDLE};
    };
} // namespace Manro
