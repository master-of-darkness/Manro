#include "VulkanRenderDevice.h"
#include "../Backend/Vulkan/VulkanContext.h"
#include <Manro/Interfaces/IWindow.h>
#include <Manro/Core/Logger.h>
#include <VkBootstrap.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace Manro::RHI {
    namespace {
        VkBufferUsageFlags RHIBufferUsageToVulkan(u32 usage) {
            VkBufferUsageFlags flags = 0;
            if (usage & BufferUsage_Vertex) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            if (usage & BufferUsage_Index) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            if (usage & BufferUsage_Uniform) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            if (usage & BufferUsage_Storage) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            if (usage & BufferUsage_Indirect) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            if (usage & BufferUsage_TransferSrc) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            if (usage & BufferUsage_TransferDst) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            return flags;
        }

        VkImageUsageFlags RHITextureUsageToVulkan(u32 usage) {
            VkImageUsageFlags flags = 0;
            if (usage & TextureUsage_Sampled) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
            if (usage & TextureUsage_Storage) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
            if (usage & TextureUsage_ColorAttachment) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (usage & TextureUsage_DepthStencilAttachment) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (usage & TextureUsage_TransferSrc) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            if (usage & TextureUsage_TransferDst) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            return flags;
        }
    }

    VulkanRenderDevice::VulkanRenderDevice(VulkanContext &context, u32 width, u32 height, bool vsync,
                                           bool manageSwapchain, u32 maxFramesInFlight)
        : m_Context(context), m_Vsync(vsync), m_ManageSwapchain(manageSwapchain),
          m_MaxFramesInFlight(std::max(1u, maxFramesInFlight)) {
        // Initialize swapchain only if we're managing it
        if (m_ManageSwapchain) {
            InitializeSwapchain(width, height, vsync);
        }

        // Create command pool
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_Context.GetGraphicsQueueFamilyIndex();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(m_Context.GetDevice(), &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
            LOG_ERROR("Failed to create command pool");
        }

        // Create command buffers
        m_CommandBuffers.resize(m_MaxFramesInFlight);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = m_MaxFramesInFlight;

        if (vkAllocateCommandBuffers(m_Context.GetDevice(), &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS) {
            LOG_ERROR("Failed to allocate command buffers");
        }

        // Create sync objects only if we're managing the swapchain
        if (m_ManageSwapchain) {
            m_ImageAvailableSemaphores.resize(m_MaxFramesInFlight);
            m_InFlightFences.resize(m_MaxFramesInFlight);
            m_RenderFinishedSemaphores.resize(m_SwapchainImages.size());

            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            VkDevice device = m_Context.GetDevice();
            for (u32 i = 0; i < m_MaxFramesInFlight; i++) {
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]);
                vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFences[i]);
            }
            for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++) {
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]);
            }
        }

        // Query device features and info
        QueryDeviceFeatures();
    }

    VulkanRenderDevice::~VulkanRenderDevice() {
        VkDevice device = m_Context.GetDevice();
        vkDeviceWaitIdle(device);

        // Cleanup sync objects (only if we created them)
        if (m_ManageSwapchain) {
            for (u32 i = 0; i < m_MaxFramesInFlight; i++) {
                vkDestroySemaphore(device, m_ImageAvailableSemaphores[i], nullptr);
                vkDestroyFence(device, m_InFlightFences[i], nullptr);
            }
            for (VkSemaphore sem: m_RenderFinishedSemaphores) {
                vkDestroySemaphore(device, sem, nullptr);
            }
        }

        // Cleanup command pool
        if (m_CommandPool) {
            vkDestroyCommandPool(device, m_CommandPool, nullptr);
        }

        // Cleanup resources using ForEach
        m_Fences.ForEach([device](VulkanFence &fence) {
            vkDestroyFence(device, fence.fence, nullptr);
        });

        m_Semaphores.ForEach([device](VulkanSemaphore &sem) {
            vkDestroySemaphore(device, sem.semaphore, nullptr);
        });

        m_Pipelines.ForEach([device](VulkanPipeline &pipeline) {
            vkDestroyPipeline(device, pipeline.pipeline, nullptr);
            vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
        });

        m_ShaderModules.ForEach([device](VulkanShaderModule &shader) {
            vkDestroyShaderModule(device, shader.module, nullptr);
        });

        m_Textures.ForEach([device, this](VulkanTexture &texture) {
            if (texture.allocation) {
                vkDestroyImageView(device, texture.view, nullptr);
                vmaDestroyImage(m_Context.GetAllocator(), texture.image, texture.allocation);
            }
        });

        m_Buffers.ForEach([this](VulkanBuffer &buffer) {
            vmaDestroyBuffer(m_Context.GetAllocator(), buffer.buffer, buffer.allocation);
        });

        // Only clean up swapchain if we created it
        if (m_ManageSwapchain) {
            CleanupSwapchain();
        }
    }

    void VulkanRenderDevice::InitializeSwapchain(u32 width, u32 height, bool vsync) {
        // Use vk-bootstrap for swapchain creation (same as old Swapchain class)
        vkb::SwapchainBuilder swapchainBuilder{
            m_Context.GetPhysicalDevice(),
            m_Context.GetDevice(),
            m_Context.GetSurface()
        };

        if (vsync) {
            swapchainBuilder
                    .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
                    .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
        } else {
            swapchainBuilder
                    .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
                    .add_fallback_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
                    .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
        }

        auto vkbSwapchainRet = swapchainBuilder
                .use_default_format_selection()
                .set_desired_extent(width, height)
                .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build();

        if (!vkbSwapchainRet) {
            LOG_ERROR("[VulkanRenderDevice] Failed to create swapchain: {}", vkbSwapchainRet.error().message());
            return;
        }

        vkb::Swapchain vkbSwapchain = vkbSwapchainRet.value();
        LOG_INFO("[VulkanRenderDevice] Swapchain created - Present mode: {}, Extent: {}x{}",
                 static_cast<int>(vkbSwapchain.present_mode),
                 vkbSwapchain.extent.width, vkbSwapchain.extent.height);

        m_Swapchain = vkbSwapchain.swapchain;
        m_SwapchainExtent = vkbSwapchain.extent;
        m_SwapchainFormat = VulkanFormatToRHI(vkbSwapchain.image_format);
        m_VkSwapchainFormat = vkbSwapchain.image_format;

        // Get images and views from vk-bootstrap
        auto imagesRet = vkbSwapchain.get_images();
        auto imageViewsRet = vkbSwapchain.get_image_views();

        if (!imagesRet || !imageViewsRet) {
            LOG_ERROR("[VulkanRenderDevice] Failed to get swapchain images/views");
            return;
        }

        m_SwapchainImages = imagesRet.value();
        m_SwapchainImageViews = imageViewsRet.value();

        LOG_INFO("[VulkanRenderDevice] Swapchain image count: {}", m_SwapchainImages.size());

        // Create swapchain texture handle for RHI
        TextureDesc desc{};
        desc.width = m_SwapchainExtent.width;
        desc.height = m_SwapchainExtent.height;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.format = m_SwapchainFormat;
        desc.usageFlags = TextureUsage_ColorAttachment | TextureUsage_TransferDst;
        desc.sampleCount = 1;

        VulkanTexture tex{};
        tex.image = m_SwapchainImages[0]; // Will update in BeginFrame
        tex.view = m_SwapchainImageViews[0];
        tex.allocation = nullptr; // Swapchain owns the image
        tex.desc = desc;

        m_SwapchainTexture = m_Textures.Insert(tex);
    }

    void VulkanRenderDevice::CleanupSwapchain() {
        VkDevice device = m_Context.GetDevice();

        for (auto view: m_SwapchainImageViews) {
            vkDestroyImageView(device, view, nullptr);
        }
        m_SwapchainImageViews.clear();
        m_SwapchainImages.clear();

        if (m_Swapchain) {
            vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
    }

    void VulkanRenderDevice::RecreateSwapchain(u32 width, u32 height) {
        if (!m_ManageSwapchain) {
            return;
        }

        VkDevice device = m_Context.GetDevice();
        vkDeviceWaitIdle(device);

        for (VkSemaphore sem: m_RenderFinishedSemaphores) {
            vkDestroySemaphore(device, sem, nullptr);
        }
        m_RenderFinishedSemaphores.clear();

        CleanupSwapchain();

        // Remove old swapchain texture handle
        if (m_SwapchainTexture.IsValid()) {
            m_Textures.Remove(m_SwapchainTexture);
        }

        InitializeSwapchain(width, height, m_Vsync);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        m_RenderFinishedSemaphores.resize(m_SwapchainImages.size());
        for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++) {
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]);
        }

        m_NeedsRecreate = false;

        LOG_INFO("[VulkanRenderDevice] Swapchain recreated: {}x{}", width, height);
    }

    void VulkanRenderDevice::QueryDeviceFeatures() {
        VkPhysicalDevice physicalDevice = m_Context.GetPhysicalDevice();

        // Get device properties
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        std::strncpy(m_AdapterInfo.name, properties.deviceName, sizeof(m_AdapterInfo.name) - 1);
        m_AdapterInfo.vendorID = properties.vendorID;
        m_AdapterInfo.deviceID = properties.deviceID;
        m_Context.GetVramStats(m_AdapterInfo.vramUsage, m_AdapterInfo.vramBudget);

        // Get device features
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(physicalDevice, &features);

        m_DeviceFeatures.geometryShader = features.geometryShader;
        m_DeviceFeatures.tessellationShader = features.tessellationShader;
        m_DeviceFeatures.multiDrawIndirect = features.multiDrawIndirect;
        m_DeviceFeatures.drawIndirectCount = true; // Vulkan 1.2+ required
        m_DeviceFeatures.computeShader = true;
        m_DeviceFeatures.timelineSemaphores = true; // Vulkan 1.2+
        m_DeviceFeatures.descriptorIndexing = true; // Vulkan 1.2+
        m_DeviceFeatures.bufferDeviceAddress = true; // Vulkan 1.2+

        // Limits
        m_DeviceFeatures.maxComputeWorkGroupInvocations = properties.limits.maxComputeWorkGroupInvocations;
        m_DeviceFeatures.maxComputeWorkGroupSize[0] = properties.limits.maxComputeWorkGroupSize[0];
        m_DeviceFeatures.maxComputeWorkGroupSize[1] = properties.limits.maxComputeWorkGroupSize[1];
        m_DeviceFeatures.maxComputeWorkGroupSize[2] = properties.limits.maxComputeWorkGroupSize[2];
        m_DeviceFeatures.maxFramebufferWidth = properties.limits.maxFramebufferWidth;
        m_DeviceFeatures.maxFramebufferHeight = properties.limits.maxFramebufferHeight;

        // Extended features would need additional queries
        m_DeviceFeatures.raytracing = false; // TODO: Query raytracing extension
        m_DeviceFeatures.meshShaders = false; // TODO: Query mesh shader extension
        m_DeviceFeatures.variableRateShading = false; // TODO: Query VRS extension

        m_AdapterInfo.features = m_DeviceFeatures;
    }

    VkFormat VulkanRenderDevice::RHIFormatToVulkan(Format format) const {
        switch (format) {
            case Format::Undefined: return VK_FORMAT_UNDEFINED;
            case Format::R8_Unorm: return VK_FORMAT_R8_UNORM;
            case Format::R8_Snorm: return VK_FORMAT_R8_SNORM;
            case Format::R8_Uint: return VK_FORMAT_R8_UINT;
            case Format::R8_Sint: return VK_FORMAT_R8_SINT;
            case Format::R8G8_Unorm: return VK_FORMAT_R8G8_UNORM;
            case Format::R8G8_Snorm: return VK_FORMAT_R8G8_SNORM;
            case Format::R8G8_Uint: return VK_FORMAT_R8G8_UINT;
            case Format::R8G8_Sint: return VK_FORMAT_R8G8_SINT;
            case Format::R8G8B8A8_Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
            case Format::R8G8B8A8_Snorm: return VK_FORMAT_R8G8B8A8_SNORM;
            case Format::R8G8B8A8_Uint: return VK_FORMAT_R8G8B8A8_UINT;
            case Format::R8G8B8A8_Sint: return VK_FORMAT_R8G8B8A8_SINT;
            case Format::R8G8B8A8_Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
            case Format::B8G8R8A8_Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
            case Format::B8G8R8A8_Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
            case Format::R16_Unorm: return VK_FORMAT_R16_UNORM;
            case Format::R16_Snorm: return VK_FORMAT_R16_SNORM;
            case Format::R16_Uint: return VK_FORMAT_R16_UINT;
            case Format::R16_Sint: return VK_FORMAT_R16_SINT;
            case Format::R16_Float: return VK_FORMAT_R16_SFLOAT;
            case Format::R16G16_Unorm: return VK_FORMAT_R16G16_UNORM;
            case Format::R16G16_Snorm: return VK_FORMAT_R16G16_SNORM;
            case Format::R16G16_Uint: return VK_FORMAT_R16G16_UINT;
            case Format::R16G16_Sint: return VK_FORMAT_R16G16_SINT;
            case Format::R16G16_Float: return VK_FORMAT_R16G16_SFLOAT;
            case Format::R16G16B16A16_Unorm: return VK_FORMAT_R16G16B16A16_UNORM;
            case Format::R16G16B16A16_Snorm: return VK_FORMAT_R16G16B16A16_SNORM;
            case Format::R16G16B16A16_Uint: return VK_FORMAT_R16G16B16A16_UINT;
            case Format::R16G16B16A16_Sint: return VK_FORMAT_R16G16B16A16_SINT;
            case Format::R16G16B16A16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case Format::R32_Uint: return VK_FORMAT_R32_UINT;
            case Format::R32_Sint: return VK_FORMAT_R32_SINT;
            case Format::R32_Float: return VK_FORMAT_R32_SFLOAT;
            case Format::R32G32_Uint: return VK_FORMAT_R32G32_UINT;
            case Format::R32G32_Sint: return VK_FORMAT_R32G32_SINT;
            case Format::R32G32_Float: return VK_FORMAT_R32G32_SFLOAT;
            case Format::R32G32B32_Uint: return VK_FORMAT_R32G32B32_UINT;
            case Format::R32G32B32_Sint: return VK_FORMAT_R32G32B32_SINT;
            case Format::R32G32B32_Float: return VK_FORMAT_R32G32B32_SFLOAT;
            case Format::R32G32B32A32_Uint: return VK_FORMAT_R32G32B32A32_UINT;
            case Format::R32G32B32A32_Sint: return VK_FORMAT_R32G32B32A32_SINT;
            case Format::R32G32B32A32_Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case Format::D16_Unorm: return VK_FORMAT_D16_UNORM;
            case Format::D32_Float: return VK_FORMAT_D32_SFLOAT;
            case Format::D24_Unorm_S8_Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
            case Format::D32_Float_S8_Uint: return VK_FORMAT_D32_SFLOAT_S8_UINT;
            case Format::BC1_Unorm: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
            case Format::BC1_Srgb: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
            case Format::BC3_Unorm: return VK_FORMAT_BC3_UNORM_BLOCK;
            case Format::BC3_Srgb: return VK_FORMAT_BC3_SRGB_BLOCK;
            case Format::BC4_Unorm: return VK_FORMAT_BC4_UNORM_BLOCK;
            case Format::BC4_Snorm: return VK_FORMAT_BC4_SNORM_BLOCK;
            case Format::BC5_Unorm: return VK_FORMAT_BC5_UNORM_BLOCK;
            case Format::BC5_Snorm: return VK_FORMAT_BC5_SNORM_BLOCK;
            case Format::BC6H_Ufloat: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
            case Format::BC6H_Sfloat: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
            case Format::BC7_Unorm: return VK_FORMAT_BC7_UNORM_BLOCK;
            case Format::BC7_Srgb: return VK_FORMAT_BC7_SRGB_BLOCK;
            default: return VK_FORMAT_UNDEFINED;
        }
    }

    Format VulkanRenderDevice::VulkanFormatToRHI(VkFormat format) const {
        switch (format) {
            case VK_FORMAT_UNDEFINED: return Format::Undefined;
            case VK_FORMAT_R8_UNORM: return Format::R8_Unorm;
            case VK_FORMAT_R8G8B8A8_UNORM: return Format::R8G8B8A8_Unorm;
            case VK_FORMAT_R8G8B8A8_SRGB: return Format::R8G8B8A8_Srgb;
            case VK_FORMAT_B8G8R8A8_UNORM: return Format::B8G8R8A8_Unorm;
            case VK_FORMAT_B8G8R8A8_SRGB: return Format::B8G8R8A8_Srgb;
            case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::R16G16B16A16_Float;
            case VK_FORMAT_R32_SFLOAT: return Format::R32_Float;
            case VK_FORMAT_R32G32_SFLOAT: return Format::R32G32_Float;
            case VK_FORMAT_R32G32B32_SFLOAT: return Format::R32G32B32_Float;
            case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::R32G32B32A32_Float;
            case VK_FORMAT_D16_UNORM: return Format::D16_Unorm;
            case VK_FORMAT_D32_SFLOAT: return Format::D32_Float;
            case VK_FORMAT_D24_UNORM_S8_UINT: return Format::D24_Unorm_S8_Uint;
            case VK_FORMAT_D32_SFLOAT_S8_UINT: return Format::D32_Float_S8_Uint;
            default: return Format::Undefined;
        }
    }

    // Buffer operations
    BufferHandle VulkanRenderDevice::CreateBuffer(const BufferDesc &desc) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = desc.size;
        bufferInfo.usage = RHIBufferUsageToVulkan(desc.usageFlags);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        if (desc.hostVisible) {
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        } else {
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }

        VulkanBuffer buf{};
        buf.desc = desc;

        if (vmaCreateBuffer(m_Context.GetAllocator(), &bufferInfo, &allocInfo,
                            &buf.buffer, &buf.allocation, nullptr) != VK_SUCCESS) {
            LOG_ERROR("Failed to create buffer");
            return {};
        }

        BufferHandle handle = m_Buffers.Insert(buf);

        // Register with command list for handle mapping
        m_CommandList.ImportBuffer(handle, buf.buffer);

        return handle;
    }

    void VulkanRenderDevice::DestroyBuffer(BufferHandle handle) {
        VulkanBuffer *buf = m_Buffers.Get(handle);
        if (!buf) return;

        vmaDestroyBuffer(m_Context.GetAllocator(), buf->buffer, buf->allocation);
        m_Buffers.Remove(handle);
    }

    void VulkanRenderDevice::WriteBuffer(BufferHandle handle, const void *data, u64 size, u64 offset) {
        VulkanBuffer *buf = m_Buffers.Get(handle);
        if (!buf || !data || size == 0) return;

        void *mapped;
        if (vmaMapMemory(m_Context.GetAllocator(), buf->allocation, &mapped) == VK_SUCCESS) {
            std::memcpy(static_cast<u8 *>(mapped) + offset, data, static_cast<size_t>(size));
            vmaUnmapMemory(m_Context.GetAllocator(), buf->allocation);
        }
    }

    void VulkanRenderDevice::ReadBuffer(BufferHandle handle, void *data, u64 size, u64 offset) {
        VulkanBuffer *buf = m_Buffers.Get(handle);
        if (!buf || !data || size == 0) return;

        void *mapped;
        if (vmaMapMemory(m_Context.GetAllocator(), buf->allocation, &mapped) == VK_SUCCESS) {
            std::memcpy(data, static_cast<u8 *>(mapped) + offset, static_cast<size_t>(size));
            vmaUnmapMemory(m_Context.GetAllocator(), buf->allocation);
        }
    }

    void VulkanRenderDevice::CopyBuffer(BufferHandle src, BufferHandle dst, u64 size, u64 srcOffset, u64 dstOffset) {
        VulkanBuffer *srcBuf = m_Buffers.Get(src);
        VulkanBuffer *dstBuf = m_Buffers.Get(dst);
        if (!srcBuf || !dstBuf) {
            LOG_ERROR("CopyBuffer: Invalid buffer handle");
            return;
        }

        VkDevice device = m_Context.GetDevice();

        // Allocate a one-time command buffer
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;
        vkCmdCopyBuffer(cmdBuffer, srcBuf->buffer, dstBuf->buffer, 1, &copyRegion);

        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        VkQueue graphicsQueue = m_Context.GetGraphicsQueue();
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, m_CommandPool, 1, &cmdBuffer);
    }

    // Texture operations
    TextureHandle VulkanRenderDevice::CreateTexture(const TextureDesc &desc) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = desc.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = desc.width;
        imageInfo.extent.height = desc.height;
        imageInfo.extent.depth = desc.depth;
        imageInfo.mipLevels = desc.mipLevels;
        imageInfo.arrayLayers = desc.arrayLayers;
        imageInfo.format = RHIFormatToVulkan(desc.format);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = RHITextureUsageToVulkan(desc.usageFlags);
        imageInfo.samples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.flags = desc.isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VulkanTexture tex{};
        tex.desc = desc;

        if (vmaCreateImage(m_Context.GetAllocator(), &imageInfo, &allocInfo,
                           &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
            LOG_ERROR("Failed to create texture");
            return {};
        }

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = tex.image;
        viewInfo.viewType = desc.isCube
                                ? VK_IMAGE_VIEW_TYPE_CUBE
                                : (desc.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D);
        viewInfo.format = RHIFormatToVulkan(desc.format);
        viewInfo.subresourceRange.aspectMask = (desc.usageFlags & TextureUsage_DepthStencilAttachment)
                                                   ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                   : VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = desc.mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = desc.arrayLayers;

        if (vkCreateImageView(m_Context.GetDevice(), &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
            LOG_ERROR("Failed to create texture view");
            vmaDestroyImage(m_Context.GetAllocator(), tex.image, tex.allocation);
            return {};
        }

        TextureHandle handle = m_Textures.Insert(tex);

        // Register with command list
        VulkanTextureBinding binding{};
        binding.image = tex.image;
        binding.view = tex.view;
        m_CommandList.ImportTexture(handle, binding);

        return handle;
    }

    void VulkanRenderDevice::DestroyTexture(TextureHandle handle) {
        if (handle == m_SwapchainTexture) return; // Don't destroy swapchain texture

        VulkanTexture *tex = m_Textures.Get(handle);
        if (!tex) return;

        if (tex->allocation) {
            vkDestroyImageView(m_Context.GetDevice(), tex->view, nullptr);
            vmaDestroyImage(m_Context.GetAllocator(), tex->image, tex->allocation);
        }
        m_Textures.Remove(handle);
    }

    void VulkanRenderDevice::UpdateTexture(TextureHandle handle, const void *data, u64 dataSize, u32 mipLevel,
                                           u32 arrayLayer) {
        VulkanTexture *tex = m_Textures.Get(handle);
        if (!tex || !data || dataSize == 0) {
            LOG_ERROR("UpdateTexture: Invalid parameters");
            return;
        }

        VkDevice device = m_Context.GetDevice();
        VmaAllocator allocator = m_Context.GetAllocator();

        // Create staging buffer
        VkBufferCreateInfo stagingBufferInfo{};
        stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufferInfo.size = dataSize;
        stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                 VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;
        VmaAllocationInfo stagingAllocResult;

        if (vmaCreateBuffer(allocator, &stagingBufferInfo, &stagingAllocInfo,
                            &stagingBuffer, &stagingAllocation, &stagingAllocResult) != VK_SUCCESS) {
            LOG_ERROR("UpdateTexture: Failed to create staging buffer");
            return;
        }

        // Copy data to staging buffer
        std::memcpy(stagingAllocResult.pMappedData, data, static_cast<size_t>(dataSize));

        // Allocate command buffer
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandPool = m_CommandPool;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        // Transition image to transfer destination
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex->image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = mipLevel;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = arrayLayer;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmdBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy buffer to image
        u32 mipWidth = std::max(1u, tex->desc.width >> mipLevel);
        u32 mipHeight = std::max(1u, tex->desc.height >> mipLevel);
        u32 mipDepth = std::max(1u, tex->desc.depth >> mipLevel);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = arrayLayer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {mipWidth, mipHeight, mipDepth};

        vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, tex->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition image to shader read optimal
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmdBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmdBuffer);

        // Submit
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        VkQueue graphicsQueue = m_Context.GetGraphicsQueue();
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        // Cleanup
        vkFreeCommandBuffers(device, m_CommandPool, 1, &cmdBuffer);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    }

    void VulkanRenderDevice::CopyTexture(TextureHandle src, TextureHandle dst, u32 srcMip, u32 dstMip, u32 srcLayer,
                                         u32 dstLayer) {
        VulkanTexture *srcTex = m_Textures.Get(src);
        VulkanTexture *dstTex = m_Textures.Get(dst);
        if (!srcTex || !dstTex) {
            LOG_ERROR("CopyTexture: Invalid texture handle");
            return;
        }

        VkDevice device = m_Context.GetDevice();

        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        // Transition source to transfer source
        VkImageMemoryBarrier srcBarrier{};
        srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        srcBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.image = srcTex->image;
        srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        srcBarrier.subresourceRange.baseMipLevel = srcMip;
        srcBarrier.subresourceRange.levelCount = 1;
        srcBarrier.subresourceRange.baseArrayLayer = srcLayer;
        srcBarrier.subresourceRange.layerCount = 1;
        srcBarrier.srcAccessMask = 0;
        srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        // Transition destination to transfer destination
        VkImageMemoryBarrier dstBarrier{};
        dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBarrier.image = dstTex->image;
        dstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        dstBarrier.subresourceRange.baseMipLevel = dstMip;
        dstBarrier.subresourceRange.levelCount = 1;
        dstBarrier.subresourceRange.baseArrayLayer = dstLayer;
        dstBarrier.subresourceRange.layerCount = 1;
        dstBarrier.srcAccessMask = 0;
        dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        std::array<VkImageMemoryBarrier, 2> barriers = {srcBarrier, dstBarrier};
        vkCmdPipelineBarrier(cmdBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, static_cast<u32>(barriers.size()), barriers.data());

        // Copy image
        u32 srcWidth = std::max(1u, srcTex->desc.width >> srcMip);
        u32 srcHeight = std::max(1u, srcTex->desc.height >> srcMip);
        u32 srcDepth = std::max(1u, srcTex->desc.depth >> srcMip);

        VkImageCopy region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = srcMip;
        region.srcSubresource.baseArrayLayer = srcLayer;
        region.srcSubresource.layerCount = 1;
        region.srcOffset = {0, 0, 0};
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = dstMip;
        region.dstSubresource.baseArrayLayer = dstLayer;
        region.dstSubresource.layerCount = 1;
        region.dstOffset = {0, 0, 0};
        region.extent = {srcWidth, srcHeight, srcDepth};

        vkCmdCopyImage(cmdBuffer,
                       srcTex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dstTex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region);

        // Transition both images to shader read optimal
        srcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dstBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        barriers = {srcBarrier, dstBarrier};
        vkCmdPipelineBarrier(cmdBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, static_cast<u32>(barriers.size()), barriers.data());

        vkEndCommandBuffer(cmdBuffer);

        // Submit
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        VkQueue graphicsQueue = m_Context.GetGraphicsQueue();
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, m_CommandPool, 1, &cmdBuffer);
    }

    // Shader modules
    ShaderModuleHandle VulkanRenderDevice::CreateShaderModule(const ShaderModuleDesc &desc) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = desc.codeSize;
        createInfo.pCode = reinterpret_cast<const u32 *>(desc.code);

        VulkanShaderModule shader{};
        shader.stage = desc.stage;

        if (vkCreateShaderModule(m_Context.GetDevice(), &createInfo, nullptr, &shader.module) != VK_SUCCESS) {
            LOG_ERROR("Failed to create shader module");
            return {};
        }

        return m_ShaderModules.Insert(shader);
    }

    void VulkanRenderDevice::DestroyShaderModule(ShaderModuleHandle handle) {
        VulkanShaderModule *shader = m_ShaderModules.Get(handle);
        if (!shader) return;

        vkDestroyShaderModule(m_Context.GetDevice(), shader->module, nullptr);
        m_ShaderModules.Remove(handle);
    }

    // Helper conversion functions for pipeline state
    namespace {
        VkBlendFactor RHIBlendFactorToVulkan(BlendFactor factor) {
            switch (factor) {
                case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
                case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
                case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
                case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
                case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
                case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
                case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
                case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
                case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
                case BlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
                case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
                case BlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
                case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
                case BlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
                default: return VK_BLEND_FACTOR_ZERO;
            }
        }

        VkBlendOp RHIBlendOpToVulkan(BlendOp op) {
            switch (op) {
                case BlendOp::Add: return VK_BLEND_OP_ADD;
                case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
                case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
                case BlendOp::Min: return VK_BLEND_OP_MIN;
                case BlendOp::Max: return VK_BLEND_OP_MAX;
                default: return VK_BLEND_OP_ADD;
            }
        }

        VkCompareOp RHICompareOpToVulkan(CompareOp op) {
            switch (op) {
                case CompareOp::Never: return VK_COMPARE_OP_NEVER;
                case CompareOp::Less: return VK_COMPARE_OP_LESS;
                case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
                case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
                case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
                case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
                case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
                case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
                default: return VK_COMPARE_OP_ALWAYS;
            }
        }

        VkStencilOp RHIStencilOpToVulkan(StencilOp op) {
            switch (op) {
                case StencilOp::Keep: return VK_STENCIL_OP_KEEP;
                case StencilOp::Zero: return VK_STENCIL_OP_ZERO;
                case StencilOp::Replace: return VK_STENCIL_OP_REPLACE;
                case StencilOp::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                case StencilOp::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
                case StencilOp::Invert: return VK_STENCIL_OP_INVERT;
                case StencilOp::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
                case StencilOp::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
                default: return VK_STENCIL_OP_KEEP;
            }
        }

        VkCullModeFlags RHICullModeToVulkan(CullMode mode) {
            switch (mode) {
                case CullMode::None: return VK_CULL_MODE_NONE;
                case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
                case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
                case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
                default: return VK_CULL_MODE_NONE;
            }
        }

        VkPolygonMode RHIPolygonModeToVulkan(PolygonMode mode) {
            switch (mode) {
                case PolygonMode::Fill: return VK_POLYGON_MODE_FILL;
                case PolygonMode::Line: return VK_POLYGON_MODE_LINE;
                case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
                default: return VK_POLYGON_MODE_FILL;
            }
        }

        VkPrimitiveTopology RHIPrimitiveTopologyToVulkan(PrimitiveTopology topology) {
            switch (topology) {
                case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                case PrimitiveTopology::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
                default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            }
        }

        VkStencilOpState RHIStencilOpStateToVulkan(const StencilOpState &state) {
            VkStencilOpState vkState{};
            vkState.failOp = RHIStencilOpToVulkan(state.failOp);
            vkState.passOp = RHIStencilOpToVulkan(state.passOp);
            vkState.depthFailOp = RHIStencilOpToVulkan(state.depthFailOp);
            vkState.compareOp = RHICompareOpToVulkan(state.compareOp);
            vkState.compareMask = state.compareMask;
            vkState.writeMask = state.writeMask;
            vkState.reference = state.reference;
            return vkState;
        }
    }

    PipelineHandle VulkanRenderDevice::CreateGraphicsPipeline(const PipelineDesc &desc) {
        VkDevice device = m_Context.GetDevice();

        // Collect shader stages
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        if (desc.vertexShader.IsValid()) {
            VulkanShaderModule *vs = m_ShaderModules.Get(desc.vertexShader);
            if (vs) {
                VkPipelineShaderStageCreateInfo stageInfo{};
                stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
                stageInfo.module = vs->module;
                stageInfo.pName = "main";
                shaderStages.push_back(stageInfo);
            }
        }

        if (desc.fragmentShader.IsValid()) {
            VulkanShaderModule *fs = m_ShaderModules.Get(desc.fragmentShader);
            if (fs) {
                VkPipelineShaderStageCreateInfo stageInfo{};
                stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stageInfo.module = fs->module;
                stageInfo.pName = "main";
                shaderStages.push_back(stageInfo);
            }
        }

        if (shaderStages.empty()) {
            LOG_ERROR("CreateGraphicsPipeline: No valid shader stages");
            return {};
        }

        // Vertex input state
        std::vector<VkVertexInputBindingDescription> bindingDescs;
        std::vector<VkVertexInputAttributeDescription> attributeDescs;

        for (u32 i = 0; i < desc.vertexBindingCount; ++i) {
            VkVertexInputBindingDescription binding{};
            binding.binding = desc.vertexBindings[i].binding;
            binding.stride = desc.vertexBindings[i].stride;
            binding.inputRate = desc.vertexBindings[i].inputRate == VertexInputRate::Vertex
                                    ? VK_VERTEX_INPUT_RATE_VERTEX
                                    : VK_VERTEX_INPUT_RATE_INSTANCE;
            bindingDescs.push_back(binding);
        }

        for (u32 i = 0; i < desc.vertexAttributeCount; ++i) {
            VkVertexInputAttributeDescription attr{};
            attr.location = desc.vertexAttributes[i].location;
            attr.binding = desc.vertexAttributes[i].binding;
            attr.format = RHIFormatToVulkan(desc.vertexAttributes[i].format);
            attr.offset = desc.vertexAttributes[i].offset;
            attributeDescs.push_back(attr);
        }

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<u32>(bindingDescs.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescs.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<u32>(attributeDescs.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();

        // Input assembly state
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = RHIPrimitiveTopologyToVulkan(desc.topology);
        inputAssembly.primitiveRestartEnable = desc.primitiveRestartEnable ? VK_TRUE : VK_FALSE;

        // Dynamic state (viewport and scissor)
        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // Viewport state (dynamic)
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterization state
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = RHIPolygonModeToVulkan(desc.rasterizer.polygonMode);
        rasterizer.lineWidth = desc.rasterizer.lineWidth;
        rasterizer.cullMode = RHICullModeToVulkan(desc.rasterizer.cullMode);
        rasterizer.frontFace = desc.rasterizer.frontCounterClockwise
                                   ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                   : VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = desc.rasterizer.depthBiasEnable ? VK_TRUE : VK_FALSE;
        rasterizer.depthBiasConstantFactor = desc.rasterizer.depthBiasConstantFactor;
        rasterizer.depthBiasClamp = desc.rasterizer.depthBiasClamp;
        rasterizer.depthBiasSlopeFactor = desc.rasterizer.depthBiasSlopeFactor;

        // Multisampling state
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);

        // Depth-stencil state
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = RHICompareOpToVulkan(desc.depthStencil.depthCompareOp);
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.front = RHIStencilOpStateToVulkan(desc.depthStencil.front);
        depthStencil.back = RHIStencilOpStateToVulkan(desc.depthStencil.back);
        depthStencil.minDepthBounds = desc.depthStencil.minDepthBounds;
        depthStencil.maxDepthBounds = desc.depthStencil.maxDepthBounds;

        // Color blend state
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
        for (u32 i = 0; i < desc.colorAttachmentCount; ++i) {
            VkPipelineColorBlendAttachmentState blendAttachment{};
            if (desc.colorBlendStates && i < desc.colorBlendStateCount) {
                const BlendState &bs = desc.colorBlendStates[i];
                blendAttachment.blendEnable = bs.blendEnable ? VK_TRUE : VK_FALSE;
                blendAttachment.srcColorBlendFactor = RHIBlendFactorToVulkan(bs.srcColorBlendFactor);
                blendAttachment.dstColorBlendFactor = RHIBlendFactorToVulkan(bs.dstColorBlendFactor);
                blendAttachment.colorBlendOp = RHIBlendOpToVulkan(bs.colorBlendOp);
                blendAttachment.srcAlphaBlendFactor = RHIBlendFactorToVulkan(bs.srcAlphaBlendFactor);
                blendAttachment.dstAlphaBlendFactor = RHIBlendFactorToVulkan(bs.dstAlphaBlendFactor);
                blendAttachment.alphaBlendOp = RHIBlendOpToVulkan(bs.alphaBlendOp);
                blendAttachment.colorWriteMask = bs.colorWriteMask;
            } else {
                // Default: no blending, write all components
                blendAttachment.blendEnable = VK_FALSE;
                blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            }
            colorBlendAttachments.push_back(blendAttachment);
        }

        if (colorBlendAttachments.empty()) {
            // Provide at least one color attachment for pipelines without explicit attachments
            VkPipelineColorBlendAttachmentState defaultAttachment{};
            defaultAttachment.blendEnable = VK_FALSE;
            defaultAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachments.push_back(defaultAttachment);
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = static_cast<u32>(colorBlendAttachments.size());
        colorBlending.pAttachments = colorBlendAttachments.data();
        colorBlending.blendConstants[0] = desc.blendConstants[0];
        colorBlending.blendConstants[1] = desc.blendConstants[1];
        colorBlending.blendConstants[2] = desc.blendConstants[2];
        colorBlending.blendConstants[3] = desc.blendConstants[3];

        // Pipeline layout (empty for now - in the future, integrate with descriptor system)
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        VkPipelineLayout pipelineLayout;
        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            LOG_ERROR("Failed to create pipeline layout");
            return {};
        }

        // Dynamic rendering info (VK_KHR_dynamic_rendering)
        std::vector<VkFormat> colorFormats;
        for (u32 i = 0; i < desc.colorAttachmentCount; ++i) {
            colorFormats.push_back(RHIFormatToVulkan(desc.colorFormats[i]));
        }

        if (colorFormats.empty()) {
            colorFormats.push_back(VK_FORMAT_B8G8R8A8_SRGB); // Default color format
        }

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = static_cast<u32>(colorFormats.size());
        renderingInfo.pColorAttachmentFormats = colorFormats.data();
        renderingInfo.depthAttachmentFormat = RHIFormatToVulkan(desc.depthStencilFormat);
        renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

        // If depth format includes stencil, set stencil attachment format
        if (desc.depthStencilFormat == Format::D24_Unorm_S8_Uint ||
            desc.depthStencilFormat == Format::D32_Float_S8_Uint) {
            renderingInfo.stencilAttachmentFormat = renderingInfo.depthAttachmentFormat;
        }

        // Create the graphics pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<u32>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE; // Using dynamic rendering
        pipelineInfo.subpass = 0;

        VkPipeline vkPipeline;
        VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vkPipeline);

        if (result != VK_SUCCESS) {
            LOG_ERROR("Failed to create graphics pipeline (VkResult: {})", static_cast<int>(result));
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            return {};
        }

        VulkanPipeline pipeline{};
        pipeline.pipeline = vkPipeline;
        pipeline.layout = pipelineLayout;
        pipeline.isCompute = false;

        return m_Pipelines.Insert(pipeline);
    }

    PipelineHandle VulkanRenderDevice::CreateComputePipeline(const PipelineDesc &desc) {
        VkDevice device = m_Context.GetDevice();

        if (!desc.computeShader.IsValid()) {
            LOG_ERROR("CreateComputePipeline: No compute shader specified");
            return {};
        }

        VulkanShaderModule *cs = m_ShaderModules.Get(desc.computeShader);
        if (!cs) {
            LOG_ERROR("CreateComputePipeline: Invalid compute shader handle");
            return {};
        }

        // Shader stage
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = cs->module;
        stageInfo.pName = "main";

        // Pipeline layout (empty for now)
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        VkPipelineLayout pipelineLayout;
        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            LOG_ERROR("Failed to create compute pipeline layout");
            return {};
        }

        // Create compute pipeline
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = pipelineLayout;

        VkPipeline vkPipeline;
        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vkPipeline);

        if (result != VK_SUCCESS) {
            LOG_ERROR("Failed to create compute pipeline (VkResult: {})", static_cast<int>(result));
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            return {};
        }

        VulkanPipeline pipeline{};
        pipeline.pipeline = vkPipeline;
        pipeline.layout = pipelineLayout;
        pipeline.isCompute = true;

        return m_Pipelines.Insert(pipeline);
    }

    void VulkanRenderDevice::DestroyPipeline(PipelineHandle handle) {
        VulkanPipeline *pipeline = m_Pipelines.Get(handle);
        if (!pipeline) return;

        if (pipeline->pipeline) {
            vkDestroyPipeline(m_Context.GetDevice(), pipeline->pipeline, nullptr);
        }
        if (pipeline->layout) {
            vkDestroyPipelineLayout(m_Context.GetDevice(), pipeline->layout, nullptr);
        }
        m_Pipelines.Remove(handle);
    }

    // Synchronization
    FenceHandle VulkanRenderDevice::CreateFence(bool signaled) {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (signaled) {
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        }

        VulkanFence fence{};
        if (vkCreateFence(m_Context.GetDevice(), &fenceInfo, nullptr, &fence.fence) != VK_SUCCESS) {
            LOG_ERROR("Failed to create fence");
            return {};
        }

        return m_Fences.Insert(fence);
    }

    void VulkanRenderDevice::DestroyFence(FenceHandle handle) {
        VulkanFence *fence = m_Fences.Get(handle);
        if (!fence) return;

        vkDestroyFence(m_Context.GetDevice(), fence->fence, nullptr);
        m_Fences.Remove(handle);
    }

    void VulkanRenderDevice::WaitForFence(FenceHandle handle, u64 timeout) {
        VulkanFence *fence = m_Fences.Get(handle);
        if (!fence) return;

        vkWaitForFences(m_Context.GetDevice(), 1, &fence->fence, VK_TRUE, timeout);
    }

    void VulkanRenderDevice::ResetFence(FenceHandle handle) {
        VulkanFence *fence = m_Fences.Get(handle);
        if (!fence) return;

        vkResetFences(m_Context.GetDevice(), 1, &fence->fence);
    }

    bool VulkanRenderDevice::IsFenceSignaled(FenceHandle handle) {
        VulkanFence *fence = m_Fences.Get(handle);
        if (!fence) return false;

        return vkGetFenceStatus(m_Context.GetDevice(), fence->fence) == VK_SUCCESS;
    }

    SemaphoreHandle VulkanRenderDevice::CreateSemaphore() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VulkanSemaphore sem{};
        if (vkCreateSemaphore(m_Context.GetDevice(), &semaphoreInfo, nullptr, &sem.semaphore) != VK_SUCCESS) {
            LOG_ERROR("Failed to create semaphore");
            return {};
        }

        return m_Semaphores.Insert(sem);
    }

    void VulkanRenderDevice::DestroySemaphore(SemaphoreHandle handle) {
        VulkanSemaphore *sem = m_Semaphores.Get(handle);
        if (!sem) return;

        vkDestroySemaphore(m_Context.GetDevice(), sem->semaphore, nullptr);
        m_Semaphores.Remove(handle);
    }

    // Frame management
    bool VulkanRenderDevice::BeginFrame() {
        // If not managing swapchain, return true (legacy Renderer handles frame management)
        if (!m_ManageSwapchain) {
            return true;
        }

        // If swapchain not created, return true to allow fallback behavior
        if (m_Swapchain == VK_NULL_HANDLE) {
            return true;
        }

        VkDevice device = m_Context.GetDevice();

        // Wait for previous frame
        vkWaitForFences(device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        // Acquire next image
        VkResult result = vkAcquireNextImageKHR(device, m_Swapchain, UINT64_MAX,
                                                m_ImageAvailableSemaphores[m_CurrentFrame],
                                                VK_NULL_HANDLE, &m_ImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            m_NeedsRecreate = true;
            return false;
        } else if (result == VK_SUBOPTIMAL_KHR) {
            // Continue with suboptimal, but mark for recreation
            m_NeedsRecreate = true;
        } else if (result != VK_SUCCESS) {
            LOG_ERROR("[VulkanRenderDevice] Failed to acquire swapchain image: {}", static_cast<int>(result));
            return false;
        }

        vkResetFences(device, 1, &m_InFlightFences[m_CurrentFrame]);

        // Update swapchain texture to current image
        if (VulkanTexture *tex = m_Textures.Get(m_SwapchainTexture)) {
            tex->image = m_SwapchainImages[m_ImageIndex];
            tex->view = m_SwapchainImageViews[m_ImageIndex];

            VulkanTextureBinding binding{};
            binding.image = tex->image;
            binding.view = tex->view;
            m_CommandList.ImportTexture(m_SwapchainTexture, binding);
        }

        // Reset and begin command buffer
        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
            LOG_ERROR("[VulkanRenderDevice] Failed to begin command buffer");
            return false;
        }

        m_CommandList.SetCommandBuffer(cmd);

        return true;
    }

    ICommandList &VulkanRenderDevice::GetCommandList() {
        return m_CommandList;
    }

    void VulkanRenderDevice::EndFrame() {
        // If not managing swapchain, skip (legacy Renderer handles frame management)
        if (!m_ManageSwapchain) {
            return;
        }

        // If swapchain not created, skip
        if (m_Swapchain == VK_NULL_HANDLE) {
            return;
        }

        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            LOG_ERROR("[VulkanRenderDevice] Failed to end command buffer");
            return;
        }

        // Submit
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_CurrentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        if (m_ImageIndex >= m_RenderFinishedSemaphores.size()) {
            LOG_ERROR("[VulkanRenderDevice] Invalid swapchain image index {} for present semaphores", m_ImageIndex);
            return;
        }

        VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_ImageIndex]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(m_Context.GetGraphicsQueue(), 1, &submitInfo, m_InFlightFences[m_CurrentFrame]) !=
            VK_SUCCESS) {
            LOG_ERROR("[VulkanRenderDevice] Failed to submit command buffer");
            return;
        }

        // Present
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapchains[] = {m_Swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &m_ImageIndex;

        VkResult result = vkQueuePresentKHR(m_Context.GetGraphicsQueue(), &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            m_NeedsRecreate = true;
        } else if (result != VK_SUCCESS) {
            LOG_ERROR("[VulkanRenderDevice] Failed to present swapchain image: {}", static_cast<int>(result));
        }

        m_CurrentFrame = (m_CurrentFrame + 1) % m_MaxFramesInFlight;
    }

    TextureHandle VulkanRenderDevice::GetSwapchainTexture() const {
        return m_SwapchainTexture;
    }

    Format VulkanRenderDevice::GetSwapchainFormat() const {
        return m_SwapchainFormat;
    }

    void VulkanRenderDevice::OnResize(u32 w, u32 h) {
        if (!m_ManageSwapchain) {
            return;
        }
        RecreateSwapchain(w, h);
    }

    AdapterInfo VulkanRenderDevice::GetAdapterInfo() const {
        return m_AdapterInfo;
    }

    const DeviceFeatures &VulkanRenderDevice::GetDeviceFeatures() const {
        return m_DeviceFeatures;
    }

    bool VulkanRenderDevice::IsFormatSupported(Format format, u32 usageFlags) const {
        VkFormat vkFormat = RHIFormatToVulkan(format);
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_Context.GetPhysicalDevice(), vkFormat, &props);

        // Simple check - could be more sophisticated
        return props.optimalTilingFeatures != 0;
    }

    BufferDesc VulkanRenderDevice::GetBufferDesc(BufferHandle handle) const {
        const VulkanBuffer *buf = m_Buffers.Get(handle);
        return buf ? buf->desc : BufferDesc{};
    }

    TextureDesc VulkanRenderDevice::GetTextureDesc(TextureHandle handle) const {
        const VulkanTexture *tex = m_Textures.Get(handle);
        return tex ? tex->desc : TextureDesc{};
    }

    void VulkanRenderDevice::SetDebugName(BufferHandle handle, const char *name) {
        if (!name) return;

        VulkanBuffer *buf = m_Buffers.Get(handle);
        if (!buf) return;

        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
        nameInfo.objectHandle = reinterpret_cast<u64>(buf->buffer);
        nameInfo.pObjectName = name;

        if (vkSetDebugUtilsObjectNameEXT) {
            vkSetDebugUtilsObjectNameEXT(m_Context.GetDevice(), &nameInfo);
        }
    }

    void VulkanRenderDevice::SetDebugName(TextureHandle handle, const char *name) {
        if (!name) return;

        VulkanTexture *tex = m_Textures.Get(handle);
        if (!tex) return;

        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
        nameInfo.objectHandle = reinterpret_cast<u64>(tex->image);
        nameInfo.pObjectName = name;

        if (vkSetDebugUtilsObjectNameEXT) {
            vkSetDebugUtilsObjectNameEXT(m_Context.GetDevice(), &nameInfo);
        }

        // Also name the image view
        VkDebugUtilsObjectNameInfoEXT viewNameInfo{};
        viewNameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        viewNameInfo.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
        viewNameInfo.objectHandle = reinterpret_cast<u64>(tex->view);
        std::string viewName = std::string(name) + "_view";
        viewNameInfo.pObjectName = viewName.c_str();

        if (vkSetDebugUtilsObjectNameEXT) {
            vkSetDebugUtilsObjectNameEXT(m_Context.GetDevice(), &viewNameInfo);
        }
    }

    void VulkanRenderDevice::SetDebugName(PipelineHandle handle, const char *name) {
        if (!name) return;

        VulkanPipeline *pipeline = m_Pipelines.Get(handle);
        if (!pipeline) return;

        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
        nameInfo.objectHandle = reinterpret_cast<u64>(pipeline->pipeline);
        nameInfo.pObjectName = name;

        if (vkSetDebugUtilsObjectNameEXT) {
            vkSetDebugUtilsObjectNameEXT(m_Context.GetDevice(), &nameInfo);
        }

        // Also name the pipeline layout
        VkDebugUtilsObjectNameInfoEXT layoutNameInfo{};
        layoutNameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        layoutNameInfo.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
        layoutNameInfo.objectHandle = reinterpret_cast<u64>(pipeline->layout);
        std::string layoutName = std::string(name) + "_layout";
        layoutNameInfo.pObjectName = layoutName.c_str();

        if (vkSetDebugUtilsObjectNameEXT) {
            vkSetDebugUtilsObjectNameEXT(m_Context.GetDevice(), &layoutNameInfo);
        }
    }

    VkBuffer VulkanRenderDevice::GetVkBuffer(BufferHandle handle) const {
        const VulkanBuffer *buf = m_Buffers.Get(handle);
        return buf ? buf->buffer : VK_NULL_HANDLE;
    }

    VulkanTextureBinding VulkanRenderDevice::GetVkTexture(TextureHandle handle) const {
        const VulkanTexture *tex = m_Textures.Get(handle);
        if (tex) {
            return VulkanTextureBinding{tex->image, tex->view};
        }
        return {};
    }
} // namespace Manro::RHI
