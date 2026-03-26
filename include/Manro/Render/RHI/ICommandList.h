#pragma once

#include <Manro/Render/RHI/RHITypes.h>
#include <span>

namespace Manro::RHI {

    class ICommandList {
    public:
        virtual ~ICommandList() = default;

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

        virtual void PushConstants(const void *data, u32 size,
                                   u32 offset = 0) = 0;

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