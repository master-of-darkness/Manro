#pragma once

#include <vk_mem_alloc.h>
#include <Manro/Core/Types.h>
#include "VkBootstrap.h"
#include "volk.h"

namespace Manro {
    class IWindow;

    class VulkanContext {
    public:
        VulkanContext(const char *appName, IWindow &window);

        ~VulkanContext();

        VulkanContext(const VulkanContext &) = delete;

        VulkanContext &operator=(const VulkanContext &) = delete;

        VkInstance GetInstance() const { return m_Instance; }

        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }

        VkDevice GetDevice() const { return m_Device; }

        VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }

        u32 GetGraphicsQueueFamilyIndex() const { return m_GraphicsQueueFamilyIndex; }

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

        void CreateSurface(IWindow &window);

        void PickPhysicalDevice();

        void CreateLogicalDevice();

        VkInstance m_Instance{nullptr};
        VkPhysicalDevice m_PhysicalDevice{nullptr};
        VkDevice m_Device{nullptr};

        vkb::Instance vkb_Instance;
        vkb::Device vkb_Device;
        vkb::PhysicalDevice vkb_PhysDev;
        VkQueue m_GraphicsQueue{nullptr};
        u32 m_GraphicsQueueFamilyIndex{0};
        VkSurfaceKHR m_Surface{nullptr};
        VmaAllocator m_Allocator{nullptr};
    };
} // namespace Manro
