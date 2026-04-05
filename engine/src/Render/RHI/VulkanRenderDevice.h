#pragma once

#include <Manro/Interfaces/IRenderDevice.h>
#include <Manro/Render/RHI/VulkanCommandList.h>
#include <Manro/Core/Handle.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <unordered_map>
#include <vector>

namespace Manro {
    class VulkanContext;
    class IWindow;
}

namespace Manro::RHI {
    struct VulkanBuffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VmaAllocation allocation{nullptr};
        BufferDesc desc{};
    };

    struct VulkanTexture {
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VmaAllocation allocation{nullptr};
        TextureDesc desc{};
    };

    struct VulkanShaderModule {
        VkShaderModule module{VK_NULL_HANDLE};
        ShaderStage stage{};
    };

    struct VulkanPipeline {
        VkPipeline pipeline{VK_NULL_HANDLE};
        VkPipelineLayout layout{VK_NULL_HANDLE};
        bool isCompute{false};
    };

    struct VulkanFence {
        VkFence fence{VK_NULL_HANDLE};
    };

    struct VulkanSemaphore {
        VkSemaphore semaphore{VK_NULL_HANDLE};
    };

    class VulkanRenderDevice final : public IRenderDevice {
    public:
        /// @param manageSwapchain If false, swapchain management is disabled (for legacy Renderer compatibility)
        VulkanRenderDevice(VulkanContext &context, u32 width, u32 height, bool vsync, bool manageSwapchain = true,
                           u32 maxFramesInFlight = 3);

        ~VulkanRenderDevice() override;

        // Buffer operations
        BufferHandle CreateBuffer(const BufferDesc &desc) override;

        void DestroyBuffer(BufferHandle handle) override;

        void WriteBuffer(BufferHandle handle, const void *data, u64 size, u64 offset = 0) override;

        void ReadBuffer(BufferHandle handle, void *data, u64 size, u64 offset = 0) override;

        void CopyBuffer(BufferHandle src, BufferHandle dst, u64 size, u64 srcOffset = 0, u64 dstOffset = 0) override;

        // Texture operations
        TextureHandle CreateTexture(const TextureDesc &desc) override;

        void DestroyTexture(TextureHandle handle) override;

        void UpdateTexture(TextureHandle handle, const void *data, u64 dataSize,
                           u32 mipLevel = 0, u32 arrayLayer = 0) override;

        void CopyTexture(TextureHandle src, TextureHandle dst,
                         u32 srcMip = 0, u32 dstMip = 0,
                         u32 srcLayer = 0, u32 dstLayer = 0) override;

        // Shader modules
        ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc &desc) override;

        void DestroyShaderModule(ShaderModuleHandle handle) override;

        // Pipelines
        PipelineHandle CreateGraphicsPipeline(const PipelineDesc &desc) override;

        PipelineHandle CreateComputePipeline(const PipelineDesc &desc) override;

        void DestroyPipeline(PipelineHandle handle) override;

        // Synchronization
        FenceHandle CreateFence(bool signaled = false) override;

        void DestroyFence(FenceHandle handle) override;

        void WaitForFence(FenceHandle handle, u64 timeout = UINT64_MAX) override;

        void ResetFence(FenceHandle handle) override;

        bool IsFenceSignaled(FenceHandle handle) override;

        SemaphoreHandle CreateSemaphore() override;

        void DestroySemaphore(SemaphoreHandle handle) override;

        // Frame management
        bool BeginFrame() override;

        ICommandList &GetCommandList() override;

        void EndFrame() override;

        // Swapchain
        TextureHandle GetSwapchainTexture() const override;

        Format GetSwapchainFormat() const override;

        void OnResize(u32 w, u32 h) override;

        // Device info & capabilities
        AdapterInfo GetAdapterInfo() const override;

        const DeviceFeatures &GetDeviceFeatures() const override;

        bool IsFormatSupported(Format format, u32 usageFlags) const override;

        // Resource introspection
        BufferDesc GetBufferDesc(BufferHandle handle) const override;

        TextureDesc GetTextureDesc(TextureHandle handle) const override;

        void SetDebugName(BufferHandle handle, const char *name) override;

        void SetDebugName(TextureHandle handle, const char *name) override;

        void SetDebugName(PipelineHandle handle, const char *name) override;

        // Internal helpers
        VulkanContext &GetVulkanContext() { return m_Context; }

        VkBuffer GetVkBuffer(BufferHandle handle) const;

        VulkanTextureBinding GetVkTexture(TextureHandle handle) const;

        // Swapchain helpers for integration
        VkSwapchainKHR GetVkSwapchain() const { return m_Swapchain; }
        VkFormat GetVkSwapchainFormat() const { return m_VkSwapchainFormat; }
        VkExtent2D GetSwapchainExtent() const { return m_SwapchainExtent; }
        size_t GetSwapchainImageCount() const { return m_SwapchainImages.size(); }
        VkImage GetSwapchainImage(u32 index) const { return m_SwapchainImages[index]; }
        VkImageView GetSwapchainImageView(u32 index) const { return m_SwapchainImageViews[index]; }
        u32 GetCurrentImageIndex() const { return m_ImageIndex; }
        u32 GetCurrentFrame() const { return m_CurrentFrame; }
        u32 GetMaxFramesInFlight() const { return m_MaxFramesInFlight; }
        bool NeedsSwapchainRecreate() const { return m_NeedsRecreate; }
        VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphores[m_CurrentFrame]; }
        VkSemaphore GetRenderFinishedSemaphore() const { return m_RenderFinishedSemaphores[m_ImageIndex]; }

    private:
        void InitializeSwapchain(u32 width, u32 height, bool vsync);

        void CleanupSwapchain();

        void RecreateSwapchain(u32 width, u32 height);

        void QueryDeviceFeatures();

        VkFormat RHIFormatToVulkan(Format format) const;

        Format VulkanFormatToRHI(VkFormat format) const;

        VulkanContext &m_Context;
        VulkanCommandList m_CommandList;

        // Swapchain
        VkSwapchainKHR m_Swapchain{VK_NULL_HANDLE};
        std::vector<VkImage> m_SwapchainImages;
        std::vector<VkImageView> m_SwapchainImageViews;
        TextureHandle m_SwapchainTexture{};
        Format m_SwapchainFormat{Format::Undefined};
        VkFormat m_VkSwapchainFormat{VK_FORMAT_UNDEFINED};
        VkExtent2D m_SwapchainExtent{};
        bool m_Vsync{true};
        bool m_NeedsRecreate{false};
        bool m_ManageSwapchain{true};

        // Frame sync
        u32 m_MaxFramesInFlight{3};
        u32 m_CurrentFrame{0};
        u32 m_ImageIndex{0};
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        // Present-wait semaphores must be tracked per swapchain image to avoid reuse hazards.
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;
        std::vector<VkCommandBuffer> m_CommandBuffers;
        VkCommandPool m_CommandPool{VK_NULL_HANDLE};

        // Resources
        SlotMap<VulkanBuffer, BufferHandle> m_Buffers;
        SlotMap<VulkanTexture, TextureHandle> m_Textures;
        SlotMap<VulkanShaderModule, ShaderModuleHandle> m_ShaderModules;
        SlotMap<VulkanPipeline, PipelineHandle> m_Pipelines;
        SlotMap<VulkanFence, FenceHandle> m_Fences;
        SlotMap<VulkanSemaphore, SemaphoreHandle> m_Semaphores;

        // Device info
        AdapterInfo m_AdapterInfo{};
        DeviceFeatures m_DeviceFeatures{};
    };
} // namespace Manro::RHI