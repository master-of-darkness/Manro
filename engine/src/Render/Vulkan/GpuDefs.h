#pragma once

#include <Manro/Core/Types.h>
#include <vector>

namespace Manro {
    enum class EBufferUsage : u32 {
        None = 0,
        Storage = 1,
        TransferSrc = 2,
        TransferDst = 4,
        ShaderAddress = 8
    };

    constexpr EBufferUsage operator|(EBufferUsage a, EBufferUsage b) {
        return static_cast<EBufferUsage>(static_cast<u32>(a) | static_cast<u32>(b));
    }

    constexpr bool HasUsage(EBufferUsage v, EBufferUsage f) {
        return (static_cast<u32>(v) & static_cast<u32>(f)) != 0;
    }

    enum class EBufferMemory : u8 {
        CpuToGpu,
        GpuOnly
    };

    struct BufferCopyRegion {
        u64 srcOffset{0};
        u64 dstOffset{0};
        u64 size{0};
    };

    struct VoxelFrameDesc {
        u32 width{0};
        u32 height{0};
        bool clearColor{false};
        u32 flightSlot{0};
        Mat4 viewProj{1.f};
        Mat4 prevViewProj{1.f};
        Vec3 cameraPos{0.f};
        float nearZ{0.1f};
        float farZ{10000.f};
    };

    struct VoxelEditDispatch {
        u64 brickBufferAddr{0};
        u64 headerAddr{0};
        u64 pageTableAddr{0};
        u64 editAddr{0};
        Vec3 worldMin{0.f};
        float brickSize{16.f};
        u32 editCount{0};
        u32 virtualDim{64};
    };

    struct VoxelGiDispatch {
        u64 brickBufferAddr{0};
        u64 headerAddr{0};
        u64 cascadeAddr{0};
        u64 cascadeDstAddr{0};
        u64 sunAddr{0};
        u32 brickCount{0};
        u32 cascadeRes{64};
        u32 cascadeCount{1};
        float brickSize{16.f};
        Vec3 worldMin{0.f};
    };
}
