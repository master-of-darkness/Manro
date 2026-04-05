#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Handles.h>

namespace Manro::RHI {

    enum class Format : u16 {
        Undefined = 0,
        // 8-bit formats
        R8_Unorm,
        R8_Snorm,
        R8_Uint,
        R8_Sint,
        R8G8_Unorm,
        R8G8_Snorm,
        R8G8_Uint,
        R8G8_Sint,
        R8G8B8A8_Unorm,
        R8G8B8A8_Snorm,
        R8G8B8A8_Uint,
        R8G8B8A8_Sint,
        R8G8B8A8_Srgb,
        B8G8R8A8_Unorm,
        B8G8R8A8_Srgb,
        // 16-bit formats
        R16_Unorm,
        R16_Snorm,
        R16_Uint,
        R16_Sint,
        R16_Float,
        R16G16_Unorm,
        R16G16_Snorm,
        R16G16_Uint,
        R16G16_Sint,
        R16G16_Float,
        R16G16B16A16_Unorm,
        R16G16B16A16_Snorm,
        R16G16B16A16_Uint,
        R16G16B16A16_Sint,
        R16G16B16A16_Float,
        // 32-bit formats
        R32_Uint,
        R32_Sint,
        R32_Float,
        R32G32_Uint,
        R32G32_Sint,
        R32G32_Float,
        R32G32B32_Uint,
        R32G32B32_Sint,
        R32G32B32_Float,
        R32G32B32A32_Uint,
        R32G32B32A32_Sint,
        R32G32B32A32_Float,
        // Depth/stencil formats
        D16_Unorm,
        D32_Float,
        D24_Unorm_S8_Uint,
        D32_Float_S8_Uint,
        // Compressed formats
        BC1_Unorm,
        BC1_Srgb,
        BC3_Unorm,
        BC3_Srgb,
        BC4_Unorm,
        BC4_Snorm,
        BC5_Unorm,
        BC5_Snorm,
        BC6H_Ufloat,
        BC6H_Sfloat,
        BC7_Unorm,
        BC7_Srgb,
    };

    enum class LoadOp : u8 {
        Load, Clear, DontCare
    };

    enum class StoreOp : u8 {
        Store, DontCare
    };

    enum class BlendFactor : u8 {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        ConstantColor,
        OneMinusConstantColor,
        ConstantAlpha,
        OneMinusConstantAlpha,
        SrcAlphaSaturate,
    };

    enum class BlendOp : u8 {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    enum class CompareOp : u8 {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    enum class StencilOp : u8 {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap,
    };

    enum class CullMode : u8 {
        None,
        Front,
        Back,
        FrontAndBack,
    };

    enum class PolygonMode : u8 {
        Fill,
        Line,
        Point,
    };

    enum class PrimitiveTopology : u8 {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
    };

    enum class VertexInputRate : u8 {
        Vertex,
        Instance,
    };

    enum class ShaderStage : u8 {
        Vertex = 0x01,
        Fragment = 0x02,
        Compute = 0x04,
        Geometry = 0x08,
        TessControl = 0x10,
        TessEval = 0x20,
    };

    inline ShaderStage operator|(ShaderStage a, ShaderStage b) {
        return static_cast<ShaderStage>(static_cast<u8>(a) | static_cast<u8>(b));
    }

    inline bool operator&(ShaderStage a, ShaderStage b) {
        return (static_cast<u8>(a) & static_cast<u8>(b)) != 0;
    }

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
        u8 clearStencil = 0;
    };

    struct Viewport {
        float x, y, width, height, minDepth, maxDepth;
    };

    struct Scissor {
        i32 x, y;
        u32 width, height;
    };

    struct BlendState {
        bool blendEnable = false;
        BlendFactor srcColorBlendFactor = BlendFactor::One;
        BlendFactor dstColorBlendFactor = BlendFactor::Zero;
        BlendOp colorBlendOp = BlendOp::Add;
        BlendFactor srcAlphaBlendFactor = BlendFactor::One;
        BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
        BlendOp alphaBlendOp = BlendOp::Add;
        u8 colorWriteMask = 0x0F; // RGBA
    };

    struct StencilOpState {
        StencilOp failOp = StencilOp::Keep;
        StencilOp passOp = StencilOp::Keep;
        StencilOp depthFailOp = StencilOp::Keep;
        CompareOp compareOp = CompareOp::Always;
        u32 compareMask = 0xFF;
        u32 writeMask = 0xFF;
        u32 reference = 0;
    };

    struct DepthStencilState {
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;
        bool stencilTestEnable = false;
        StencilOpState front{};
        StencilOpState back{};
        float minDepthBounds = 0.0f;
        float maxDepthBounds = 1.0f;
    };

    struct RasterizerState {
        PolygonMode polygonMode = PolygonMode::Fill;
        CullMode cullMode = CullMode::Back;
        bool frontCounterClockwise = true;
        bool depthBiasEnable = false;
        float depthBiasConstantFactor = 0.0f;
        float depthBiasClamp = 0.0f;
        float depthBiasSlopeFactor = 0.0f;
        float lineWidth = 1.0f;
    };

    struct VertexInputAttribute {
        u32 location;
        u32 binding;
        Format format;
        u32 offset;
    };

    struct VertexInputBinding {
        u32 binding;
        u32 stride;
        VertexInputRate inputRate;
    };
} // namespace Manro::RHI