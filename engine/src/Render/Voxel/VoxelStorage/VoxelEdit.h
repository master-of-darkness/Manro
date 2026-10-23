#pragma once

#include <Manro/Core/Types.h>

namespace Manro {
    struct VoxelEditCmd_t {
        Vec3 pos{};
        float radius{0.f};
        u32 material{0};
        u32 op{0};
        u32 _pad0{0};
        u32 _pad1{0};
    };

    struct VoxelEditPushConstants_t {
        u64 brickBufferAddr{0};
        u64 headerAddr{0};
        u64 pageTableAddr{0};
        u64 editAddr{0};
        Vec3 worldMin{0.f};
        float brickSize{16.f};
        u32 editCount{0};
        u32 virtualDim{0};
    };
}
