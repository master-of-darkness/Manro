#pragma once

#include <Manro/Interfaces/Interface.h>
#include <Manro/Render/RHI/RHITypes.h>
#include <span>

namespace Manro::RHI {
    struct ZPrepassPassState;
    struct PbrPassState;
    struct CompositePassState;
    struct SkyboxPassState;

    enum class GraphicsBackend : u8 {
        Vulkan = 0
    };

    class ICommandList : public ::Manro::Interface {
    public:
        virtual ~ICommandList() = default;

        virtual GraphicsBackend GetBackendType() const = 0;

        // Backend-native command list handle encoded as u64.
        virtual u64 GetNativeHandle() const = 0;

        // Import backend-native graphics pipeline objects into command-list lookup tables
        virtual void ImportGraphicsPipeline(PipelineHandle handle, u64 pipeline, u64 layout) = 0;

        virtual void ExecuteZPrepass(const ZPrepassPassState &state) = 0;

        virtual void ExecutePbrPass(const PbrPassState &state) = 0;

        virtual void ExecuteCompositePass(const CompositePassState &state) = 0;

        virtual void ExecuteSkyboxPass(const SkyboxPassState &state) = 0;

        virtual void BeginRendering(std::span<const ColorAttachment> color,
                                    const DepthAttachment *depth = nullptr) = 0;

        virtual void EndRendering() = 0;

        virtual void SetViewport(const Viewport &vp) = 0;

        virtual void SetScissor(const Scissor &sc) = 0;

        virtual void BindPipeline(PipelineHandle pso) = 0;

        virtual void SetDepthBias(float constant, float clamp, float slope) = 0;

        virtual void BindVertexBuffer(u32 slot, BufferHandle buf, u64 offset = 0) = 0;

        virtual void BindIndexBuffer(BufferHandle buf, u64 offset = 0) = 0;

        virtual void BindDescriptorSets(u32 firstSet, const void **sets, u32 setCount) = 0;

        virtual void PushConstants(const void *data, u32 size, u32 offset = 0) = 0;

        virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1,
                                 u32 firstIndex = 0, i32 vertexOffset = 0,
                                 u32 firstInstance = 0) = 0;

        virtual void DrawIndexedIndirectCount(BufferHandle indirectBuf, u64 indirectOffset,
                                              BufferHandle countBuf, u64 countOffset,
                                              u32 maxDrawCount, u32 stride) = 0;

        virtual void Dispatch(u32 x, u32 y, u32 z) = 0;

        virtual void TextureBarrier(TextureHandle tex,
                                    u32 srcStageMask, u32 srcAccessMask,
                                    u32 dstStageMask, u32 dstAccessMask,
                                    u32 oldLayout, u32 newLayout) = 0;

        virtual void BufferBarrier(BufferHandle buf,
                                   u32 srcStageMask, u32 srcAccessMask,
                                   u32 dstStageMask, u32 dstAccessMask) = 0;

        virtual void FillBuffer(BufferHandle buf, u64 offset, u64 size, u32 value) = 0;

        virtual void BeginDebugLabel(const char *name) = 0;

        virtual void EndDebugLabel() = 0;
    };
} // namespace Manro::RHI