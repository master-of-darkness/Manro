#pragma once

#include <Manro/Interfaces/Interface.h>
#include <Manro/Interfaces/ICommandList.h>
#include <Manro/Render/RHI/RHITypes.h>
#include <span>
#include <string_view>

namespace Manro {
    class IWindow;
}

namespace Manro::RHI {

    struct BufferDesc {
        u64 size{0};
        u32 usageFlags{0};
    };

    struct TextureDesc {
        u32 width{1}, height{1};
        Format format{Format::Undefined};
        u32 usageFlags{0};
        u32 sampleCount{1};
    };

    struct PipelineDesc {
        Format colorFormat{Format::Undefined};
    };

    struct AdapterInfo {
        char name[256]{};
        u64 vramBudget{0};
        u64 vramUsage{0};
    };

    class IRenderDevice : public ::Manro::Interface {
    public:
        virtual ~IRenderDevice() = default;

        virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;

        virtual void DestroyBuffer(BufferHandle handle) = 0;

        virtual void WriteBuffer(BufferHandle handle, const void *data, u64 size, u64 offset = 0) = 0;

        virtual TextureHandle CreateTexture(const TextureDesc &desc) = 0;

        virtual void DestroyTexture(TextureHandle handle) = 0;

        virtual PipelineHandle CreateGraphicsPipeline(const PipelineDesc &desc) = 0;

        virtual PipelineHandle CreateComputePipeline(const PipelineDesc &desc) = 0;

        virtual void DestroyPipeline(PipelineHandle handle) = 0;

        virtual bool BeginFrame() = 0;

        virtual ICommandList &GetCommandList() = 0;

        virtual void EndFrame() = 0;

        virtual TextureHandle GetSwapchainTexture() const = 0;

        virtual Format GetSwapchainFormat() const = 0;

        virtual void OnResize(u32 w, u32 h) = 0;

        virtual AdapterInfo GetAdapterInfo() const = 0;

        static Scope<IRenderDevice> CreateVulkan(::Manro::IWindow &window,
                                                 u32 width, u32 height,
                                                 bool vsync = true,
                                                 const AdapterInfo *pAdapterInfo = nullptr);
    };

} // namespace Manro::RHI