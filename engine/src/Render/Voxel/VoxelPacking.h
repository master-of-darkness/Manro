#pragma once

#include <Manro/Core/Types.h>

namespace Manro {
    static constexpr u32 kVoxelBrickEdge = 16u;
    static constexpr u32 kVoxelBrickVoxels = 4096u;
    static constexpr u32 kVoxelBrickMatWords = 2048u;
    static constexpr u32 kVoxelBrickOccWords = 128u;
    static constexpr u32 kVoxelBrickWords = 2176u;
    static constexpr u32 kVoxelBrickLightWords = 1024u;
    static constexpr u32 kVoxelBrickLightBytes = 4096u;
    static constexpr u32 kVoxelBrickExWords = 3200u;
    static constexpr u32 kVoxelFaceCacheStride = 16384u;
    static constexpr u32 kVoxelFaceCacheOpaqueIdx = 16383u;
    static constexpr u32 kVoxelSpillQuota = 512u;
    static constexpr u32 kVoxelSpillWords = 3u;
    static constexpr u32 kVoxelSpillDescWords = 2u;

    struct VoxelBrickHeader_t {
        Vec3 origin{};
        u32 pageIndex{0};
        u32 flags{0};
        u32 paletteBase{0};
        u32 _pad0{0};
        u32 _pad1{0};
    };

    static_assert(sizeof(VoxelBrickHeader_t) == 32);

    static inline u32 PackVoxelVertex(u32 x, u32 y, u32 z, u32 n, u32 ao, u32 mat) {
        return (x & 0x3Fu) | ((y & 0x3Fu) << 6u) | ((z & 0x3Fu) << 12u) |
               ((n & 0x7u) << 18u) | ((ao & 0x3u) << 21u) | ((mat & 0x1FFu) << 23u);
    }
}
