#pragma once

// CPU mirrors of shaders/voxel_common.slang.
// Push constant is ONLY VoxelFrameRoot_t (~40B, under the 128B limit).
// Everything else lives in the BDA VoxelFrameParams_t buffer.
// Keep field order identical to the Slang structs.

#include <Manro/Core/Types.h>

namespace Manro {
    // One 16^3 brick: 4096 uint16 material IDs + 128 uint32 occupancy bits.
    static constexpr u32 kVoxelBrickEdge = 16u;
    static constexpr u32 kVoxelBrickVoxels = 4096u;
    static constexpr u32 kVoxelBrickMatWords = 2048u;
    static constexpr u32 kVoxelBrickOccWords = 128u;
    static constexpr u32 kVoxelBrickWords = 2176u;

    struct VoxelBrickHeader_t {
        Vec3 origin{};
        u32 pageIndex{0};
        u32 flags{0}; // bit 0: resident, bit 1: dirty
        u32 paletteBase{0};
        u32 _pad0{0};
        u32 _pad1{0};
    };
    static_assert(sizeof(VoxelBrickHeader_t) == 32);

    // Must match VoxelFrameRoot in voxel_common.slang.
    struct VoxelFrameRoot_t {
        u64 brickBufferAddr{0};
        u64 headerAddr{0};
        u64 pageTableAddr{0};
        u64 visibilityAddr{0};
        u64 frameAddr{0};
        u64 debugAddr{0};
        u64 faceCacheAddr{0}; // uint[4096] per resident brick: [0]=faceCount, [1..]=packed faces
        u64 tileTableAddr{0}; // uint32[32768*6]: tile layer per (state, face)
        u64 blockFlagsAddr{0}; // uint32[32768]: kBlockFlag* per block state id
        u64 shapeTableAddr{0}; // uint32[32768*2]: shapeMin/shapeMax per state, 0..16 bytes packed
        u64 uvTableAddr{0}; // uint32[32768*6]: uv sub-rect per (state, face), u0|v0<<8|u1<<16|v1<<24
    };
    static_assert(sizeof(VoxelFrameRoot_t) == 88);

    // Must match VoxelFrameParams in voxel_common.slang (std430, RowMajor mat4).
    struct VoxelFrameParams_t {
        Mat4 viewProj{1.f};
        Mat4 prevViewProj{1.f};
        Vec3 cameraPos{0.f};
        u32 brickCount{0};
        Vec3 worldMin{0.f};
        float brickSize{16.f};
        u32 maxDrawDistance{10000};
        u32 enableHiZ{0}; // reserved: software HiZ removed (ZCULL handles it)
        u64 paletteAddr{0};
        u64 sunAddr{0};
        u64 giAddr{0};
        u32 giEnabled{0};
        u32 shadowsEnabled{0};
        u32 debugEnabled{0};
        u32 useBackface{1}; // mesh per-face backface cull (degenerate quads)
        u32 useFrustum{1}; // task brick frustum + distance cull
        u32 virtualDim{64}; // bricks per axis: task cross-brick page lookups
    };

    struct VoxelEditCmd_t {
        Vec3 pos{};
        float radius{0.f};
        u32 material{0};
        u32 op{0}; // 0 = erase, 1 = write
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

    struct VoxelGiPushConstants_t {
        u64 brickBufferAddr{0};
        u64 headerAddr{0};
        u64 cascadeAddr{0};
        u64 sunAddr{0};
        u32 brickCount{0};
        u32 cascadeRes{64};
        u32 cascadeCount{3};
        float brickSize{16.f};
        Vec3 worldMin{0.f};
    };

    // Matches PackVoxelVertex() in voxel_common.slang.
    static inline u32 PackVoxelVertex(u32 x, u32 y, u32 z, u32 n, u32 ao, u32 mat) {
        return (x & 0x3Fu) | ((y & 0x3Fu) << 6u) | ((z & 0x3Fu) << 12u) |
               ((n & 0x7u) << 18u) | ((ao & 0x3u) << 21u) | ((mat & 0x1FFu) << 23u);
    }
} // namespace Manro
