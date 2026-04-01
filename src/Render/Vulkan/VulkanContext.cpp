#include "../Backend/Vulkan/VulkanContext.h"
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <Manro/Interfaces/IWindow.h>
#include <Manro/Core/Logger.h>
#include <volk.h>

namespace Manro {
    VulkanContext::VulkanContext(const char *appName, IWindow &window) {
        if (volkInitialize() != VK_SUCCESS) {
            LOG_ERROR("Failed to initialize volk!");
            return;
        }
        CreateInstance(appName);
        CreateSurface(window);
        PickPhysicalDevice();
        CreateLogicalDevice();
    }

    VulkanContext::~VulkanContext() {
        if (m_Allocator) {
            vmaDestroyAllocator(m_Allocator);
            m_Allocator = nullptr;
        }
        if (vkb_Device.device) {
            vkb::destroy_device(vkb_Device);
        }
        if (m_Surface) {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
            m_Surface = nullptr;
        }
        if (vkb_Instance.instance) {
            vkb::destroy_instance(vkb_Instance);
        }
    }

    void VulkanContext::CreateInstance(const char *appName) {
        vkb::InstanceBuilder builder;

        u32 count = 0;
        const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        for (u32 i = 0; i < count; ++i)
            builder.enable_extension(extensions[i]);

        auto inst_ret = builder
                .set_app_name(appName)
                .request_validation_layers(
#if defined(NDEBUG)
    false
#else
    true
#endif
                )
                .require_api_version(1, 4, 0)
                .use_default_debug_messenger()
                .build();

        if (!inst_ret) return;

        vkb_Instance = inst_ret.value();
        m_Instance = vkb_Instance.instance;
        volkLoadInstance(m_Instance);
    }

    void VulkanContext::CreateSurface(IWindow &window) {
        SDL_Window *sdlWindow = static_cast<SDL_Window *>(window.GetNativeHandle());
        if (!SDL_Vulkan_CreateSurface(sdlWindow, m_Instance, nullptr, &m_Surface)) {
            LOG_ERROR("Failed to create SDL3 Vulkan surface: {}", SDL_GetError());
        }
    }

    void VulkanContext::PickPhysicalDevice() {
        vkb::PhysicalDeviceSelector selector{vkb_Instance};

        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.descriptorIndexing = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;
        features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        features12.drawIndirectCount = VK_TRUE;
        features12.timelineSemaphore = VK_TRUE;

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        features13.maintenance4 = VK_TRUE;

        VkPhysicalDeviceVulkan11Features features11{};
        features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        features11.shaderDrawParameters = VK_TRUE;
        features11.storageBuffer16BitAccess = VK_TRUE;
        features11.uniformAndStorageBuffer16BitAccess = VK_TRUE;

        VkPhysicalDeviceFeatures baseFeatures{};
        baseFeatures.multiDrawIndirect = VK_TRUE;
        baseFeatures.drawIndirectFirstInstance = VK_TRUE;
        baseFeatures.samplerAnisotropy = VK_TRUE;
        baseFeatures.shaderInt16 = VK_TRUE;

        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
        rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rayQueryFeatures.rayQuery = VK_TRUE;

        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
        asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        asFeatures.accelerationStructure = VK_TRUE;

        auto phys_ret = selector
                .set_surface(m_Surface)
                .set_required_features(baseFeatures)
                .set_required_features_11(features11)
                .set_required_features_12(features12)
                .set_required_features_13(features13)
                .add_required_extension("VK_KHR_buffer_device_address")
                .add_required_extension("VK_KHR_dynamic_rendering")
                .add_required_extension("VK_KHR_synchronization2")
                .add_required_extension("VK_KHR_ray_query")
                .add_required_extension("VK_KHR_acceleration_structure")
                .add_required_extension("VK_KHR_deferred_host_operations")
                .add_required_extension_features(rayQueryFeatures)
                .add_required_extension_features(asFeatures)
                .select();

        if (!phys_ret) {
            LOG_ERROR("Failed to select physical device: {}", phys_ret.error().message());
            return;
        }

        vkb_PhysDev = phys_ret.value();
        m_PhysicalDevice = vkb_PhysDev.physical_device;
    }

    void VulkanContext::CreateLogicalDevice() {
        vkb::DeviceBuilder device_builder{vkb_PhysDev};

        auto dev_ret = device_builder.build();
        if (!dev_ret) {
            LOG_ERROR("Failed to create Vulkan logical device!");
            return;
        }

        vkb_Device = dev_ret.value();
        m_Device = vkb_Device.device;
        volkLoadDevice(m_Device);

        m_GraphicsQueue = vkb_Device.get_queue(vkb::QueueType::graphics).value();
        m_GraphicsQueueFamilyIndex = vkb_Device.get_queue_index(vkb::QueueType::graphics).value();

        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4;
        allocatorInfo.physicalDevice = m_PhysicalDevice;
        allocatorInfo.device = m_Device;
        allocatorInfo.instance = m_Instance;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

        VmaVulkanFunctions vulkanFunctions{};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;

        vmaCreateAllocator(&allocatorInfo, &m_Allocator);
    }

    VkSampleCountFlagBits VulkanContext::GetMaxUsableSampleCount() const {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);
        VkSampleCountFlags counts =
                props.limits.framebufferColorSampleCounts &
                props.limits.framebufferDepthSampleCounts;
        if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
        if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
        if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
        if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
        if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
        if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
        return VK_SAMPLE_COUNT_1_BIT;
    }

    void VulkanContext::GetVramStats(u64& usage, u64& budget) const {
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
        vmaGetHeapBudgets(m_Allocator, budgets);
        usage = 0;
        budget = 0;
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);
        for (u32 i = 0; i < memProps.memoryHeapCount; ++i) {
            if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                usage += budgets[i].usage;
                budget += budgets[i].budget;
            }
        }
    }
} // namespace Manro
