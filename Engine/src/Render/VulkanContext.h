#pragma once
#include <vk_mem_alloc.h>
#include <Core/Types.h>

#include "VkBootstrap.h"

namespace Engine {
    class IWindow;

    class VulkanContext {
    public:
        VulkanContext();

        ~VulkanContext();

        void Initialize(const char *appName);

        void Shutdown();

        VkInstance GetInstance() const { return m_Instance; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkDevice GetDevice() const { return m_Device; }
        VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        u32 GetGraphicsQueueFamilyIndex() const { return m_GraphicsQueueFamilyIndex; }
        VmaAllocator GetAllocator() const { return m_Allocator; }

        void CreateSurface(IWindow *window);

        VkSurfaceKHR GetSurface() const { return m_Surface; }

        VkSampleCountFlagBits GetMaxUsableSampleCount() const;

    private:
        void CreateInstance(const char *appName);

        void PickPhysicalDevice();

        void CreateLogicalDevice();

        VkInstance m_Instance{nullptr};
        VkPhysicalDevice m_PhysicalDevice{nullptr};
        VkDevice m_Device{nullptr};

        vkb::Instance vkb_Instance;
        vkb::PhysicalDevice vkb_PhysDev;
        VkQueue m_GraphicsQueue{nullptr};
        u32 m_GraphicsQueueFamilyIndex{0};
        VkSurfaceKHR m_Surface{nullptr};
        VmaAllocator m_Allocator{nullptr};
    };
} // namespace Engine
