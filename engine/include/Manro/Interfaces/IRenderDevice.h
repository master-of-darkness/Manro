#pragma once

#include <Manro/Interfaces/Interface.h>
#include <Manro/Interfaces/ICommandList.h>
#include <Manro/Render/RHI/RHITypes.h>
#include <span>
#include <string_view>

namespace Manro {
    class IWindow;
    class VulkanContext;
}

namespace Manro::RHI {
    // Buffer usage flags
    enum BufferUsage : u32 {
        BufferUsage_None = 0,
        BufferUsage_Vertex = 1 << 0,
        BufferUsage_Index = 1 << 1,
        BufferUsage_Uniform = 1 << 2,
        BufferUsage_Storage = 1 << 3,
        BufferUsage_Indirect = 1 << 4,
        BufferUsage_TransferSrc = 1 << 5,
        BufferUsage_TransferDst = 1 << 6,
    };

    // Texture usage flags
    enum TextureUsage : u32 {
        TextureUsage_None = 0,
        TextureUsage_Sampled = 1 << 0,
        TextureUsage_Storage = 1 << 1,
        TextureUsage_ColorAttachment = 1 << 2,
        TextureUsage_DepthStencilAttachment = 1 << 3,
        TextureUsage_TransferSrc = 1 << 4,
        TextureUsage_TransferDst = 1 << 5,
    };

    struct BufferDesc {
        u64 size{0};
        u32 usageFlags{0};
        bool hostVisible{false}; // CPU accessible
    };

    struct TextureDesc {
        u32 width{1}, height{1}, depth{1};
        u32 mipLevels{1};
        u32 arrayLayers{1};
        Format format{Format::Undefined};
        u32 usageFlags{0};
        u32 sampleCount{1};
        bool isCube{false};
    };

    struct ShaderModuleDesc {
        const void *code;
        u64 codeSize;
        ShaderStage stage;
        const char *entryPoint = "main";
    };

    struct PipelineDesc {
        // Shader stages
        ShaderModuleHandle vertexShader{};
        ShaderModuleHandle fragmentShader{};
        ShaderModuleHandle computeShader{};

        // Vertex input
        const VertexInputAttribute *vertexAttributes = nullptr;
        u32 vertexAttributeCount = 0;
        const VertexInputBinding *vertexBindings = nullptr;
        u32 vertexBindingCount = 0;

        // Input assembly
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        bool primitiveRestartEnable = false;

        // Rasterization
        RasterizerState rasterizer{};

        // Depth/Stencil
        DepthStencilState depthStencil{};

        // Color blending
        const BlendState *colorBlendStates = nullptr;
        u32 colorBlendStateCount = 0;
        float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        // Render targets
        Format colorFormats[8] = {Format::Undefined};
        u32 colorAttachmentCount = 0;
        Format depthStencilFormat = Format::Undefined;

        // Multisampling
        u32 sampleCount = 1;
    };

    struct DeviceFeatures {
        bool raytracing = false;
        bool meshShaders = false;
        bool variableRateShading = false;
        bool timelineSemaphores = false;
        bool descriptorIndexing = false;
        bool bufferDeviceAddress = false;
        bool geometryShader = false;
        bool tessellationShader = false;
        bool computeShader = false;
        bool multiDrawIndirect = false;
        bool drawIndirectCount = false;
        u32 maxComputeWorkGroupInvocations = 0;
        u32 maxComputeWorkGroupSize[3] = {0, 0, 0};
        u32 maxFramebufferWidth = 0;
        u32 maxFramebufferHeight = 0;
    };

    struct AdapterInfo {
        char name[256]{};
        u32 vendorID{0};
        u32 deviceID{0};
        u64 vramBudget{0};
        u64 vramUsage{0};
        DeviceFeatures features{};
    };

    class IRenderDevice : public ::Manro::Interface {
    public:
        virtual ~IRenderDevice() = default;

        virtual GraphicsBackend GetBackendType() const = 0;

        // Buffer operations
        virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;

        virtual void DestroyBuffer(BufferHandle handle) = 0;

        virtual void WriteBuffer(BufferHandle handle, const void *data, u64 size, u64 offset = 0) = 0;

        virtual void ReadBuffer(BufferHandle handle, void *data, u64 size, u64 offset = 0) = 0;

        virtual void CopyBuffer(BufferHandle src, BufferHandle dst, u64 size, u64 srcOffset = 0, u64 dstOffset = 0) = 0;

        // Texture operations
        virtual TextureHandle CreateTexture(const TextureDesc &desc) = 0;

        virtual void DestroyTexture(TextureHandle handle) = 0;

        virtual void UpdateTexture(TextureHandle handle, const void *data, u64 dataSize,
                                   u32 mipLevel = 0, u32 arrayLayer = 0) = 0;

        virtual void CopyTexture(TextureHandle src, TextureHandle dst,
                                 u32 srcMip = 0, u32 dstMip = 0,
                                 u32 srcLayer = 0, u32 dstLayer = 0) = 0;

        // Shader modules
        virtual ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc &desc) = 0;

        virtual void DestroyShaderModule(ShaderModuleHandle handle) = 0;

        // Pipelines
        virtual PipelineHandle CreateGraphicsPipeline(const PipelineDesc &desc) = 0;

        virtual PipelineHandle CreateComputePipeline(const PipelineDesc &desc) = 0;

        virtual void DestroyPipeline(PipelineHandle handle) = 0;

        // Synchronization
        virtual FenceHandle CreateFence(bool signaled = false) = 0;

        virtual void DestroyFence(FenceHandle handle) = 0;

        virtual void WaitForFence(FenceHandle handle, u64 timeout = UINT64_MAX) = 0;

        virtual void ResetFence(FenceHandle handle) = 0;

        virtual bool IsFenceSignaled(FenceHandle handle) = 0;

        virtual SemaphoreHandle CreateSemaphore() = 0;

        virtual void DestroySemaphore(SemaphoreHandle handle) = 0;

        // Frame management
        virtual bool BeginFrame() = 0;

        virtual ICommandList &GetCommandList() = 0;

        virtual void EndFrame() = 0;

        // Swapchain
        virtual TextureHandle GetSwapchainTexture() const = 0;

        virtual Format GetSwapchainFormat() const = 0;

        virtual void OnResize(u32 w, u32 h) = 0;

        virtual bool NeedsSwapchainRecreate() const = 0;

        virtual u32 GetCurrentFrameIndex() const = 0;

        virtual u32 GetCurrentImageIndex() const = 0;

        virtual u32 GetSwapchainWidth() const = 0;

        virtual u32 GetSwapchainHeight() const = 0;

        // Backend native swapchain image handles and format encoded as u64.
        virtual u64 GetNativeSwapchainImage(u32 index) const = 0;

        virtual u64 GetNativeSwapchainImageView(u32 index) const = 0;

        virtual u64 GetNativeSwapchainFormat() const = 0;

        // Device info & capabilities
        virtual AdapterInfo GetAdapterInfo() const = 0;

        virtual const DeviceFeatures &GetDeviceFeatures() const = 0;

        virtual bool IsFormatSupported(Format format, u32 usageFlags) const = 0;

        // Resource introspection
        virtual BufferDesc GetBufferDesc(BufferHandle handle) const = 0;

        virtual TextureDesc GetTextureDesc(TextureHandle handle) const = 0;

        virtual void SetDebugName(BufferHandle handle, const char *name) = 0;

        virtual void SetDebugName(TextureHandle handle, const char *name) = 0;

        virtual void SetDebugName(PipelineHandle handle, const char *name) = 0;

        // Factory methods
        static Scope<IRenderDevice> CreateVulkan(::Manro::IWindow &window,
                                                 u32 width, u32 height,
                                                 bool vsync = true,
                                                 const AdapterInfo *pAdapterInfo = nullptr);

        /// Create VulkanRenderDevice using existing context
        /// @param manageSwapchain If false, swapchain is not created (for use alongside legacy Renderer)
        static Scope<IRenderDevice> CreateVulkan(::Manro::VulkanContext &context,
                                                 u32 width, u32 height,
                                                 bool vsync = true,
                                                 bool manageSwapchain = true,
                                                 u32 maxFramesInFlight = 3);
    };
} // namespace Manro::RHI