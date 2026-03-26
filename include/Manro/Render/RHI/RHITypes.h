#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Handle.h>

namespace Manro::RHI {

    struct BufferTag {
    };
    using BufferHandle = Handle<BufferTag>;
    struct TextureTag {
    };
    using TextureHandle = Handle<TextureTag>;
    struct PipelineTag {
    };
    using PipelineHandle = Handle<PipelineTag>;
    struct SamplerTag {
    };

    enum class Format : u16 {
        Undefined = 0,
        R8G8B8A8_Unorm, R16G16B16A16_Float,
        D32_Float, D24_Unorm_S8_Uint,
        B8G8R8A8_Unorm,
    };

    enum class LoadOp : u8 {
        Load, Clear, DontCare
    };
    enum class StoreOp : u8 {
        Store, DontCare
    };

    struct ColorAttachment {
        TextureHandle texture;
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        float clear[4]{0.f, 0.f, 0.f, 1.f};
    };

    struct DepthAttachment {
        TextureHandle texture;
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::DontCare;
        float clearDepth = 1.f;
    };

    struct Viewport {
        float x, y, width, height, minDepth, maxDepth;
    };
    struct Scissor {
        i32 x, y;
        u32 width, height;
    };

} // namespace Manro::RHI