#pragma once

#include <Manro/Core/Types.h>

namespace Manro {

    static constexpr u32 kVoxelBrickEdge = 16u;
    static constexpr u32 kVoxelBrickVoxels = 4096u;
    static constexpr u32 kVoxelBrickMatWords = 2048u;
    static constexpr u32 kVoxelBrickOccWords = 128u;
    static constexpr u32 kVoxelBrickWords = 2176u;
    // Packed flat light attached to the brick: 1 byte/voxel
    // (sky high nibble, block low), 4096 bytes = 1024 u32 words.
    static constexpr u32 kVoxelBrickLightWords = 1024u;
    static constexpr u32 kVoxelBrickLightBytes = 4096u;
    // Full brick stride on GPU: mats + occ + flat light. Single sparse
    // buffer, single staging copy, single barrier (no light bricks).
    static constexpr u32 kVoxelBrickExWords = 3200u;
    // Face cache: word0 = face count, then 4 words per face:
    //   w0 = iter(12b) | face(3b) | mat(15b)
    //   w1 = v0[sky4,blk4,ao2] | v1[...]<<10 | v2[...]<<20  (30b)
    //   w2 = v3[sky4,blk4,ao2]
    //   w3 = sub-box index + 1 (0 = single union-AABB quad path)
    // (Baked once by the task shader on rebuild: smooth light + AO.)
    // Must match kVoxelFaceCacheStride in shaders/voxel_common.slang.
    static constexpr u32 kVoxelFaceCacheStride = 16384u;
    static constexpr u32 kVoxelFaceCacheOpaqueIdx = 16383u;

    struct VoxelBrickHeader_t {
        Vec3 origin{};
        u32 pageIndex{0};
        u32 flags{0};
        u32 paletteBase{0};
        u32 _pad0{0};
        u32 _pad1{0};
    };
    static_assert(sizeof(VoxelBrickHeader_t) == 32);

    struct VoxelFragParams_t {
        Vec4 sunDir{0.f};
        Vec4 sunColor{1.f};
        u64 giAddr{0};
        u32 giEnabled{0};
        u32 _pad0{0};
    };
    static_assert(sizeof(VoxelFragParams_t) == 48);

    struct VoxelFrameRoot_t {
        u64 brickBufferAddr{0};
        u64 headerAddr{0};
        u64 pageTableAddr{0};
        u64 visibilityAddr{0};
        u64 frameAddr{0};
        u64 debugAddr{0};
        u64 faceCacheAddr{0};
        u64 tileTableAddr{0};
        u64 blockFlagsAddr{0};
        u64 shapeTableAddr{0};
        u64 uvTableAddr{0};
        u64 fragParamsAddr{0};
        u64 boxTableAddr{0};
    };
    static_assert(sizeof(VoxelFrameRoot_t) == 104);

    struct VoxelFrameParams_t {
        Mat4 viewProj{1.f};
        Mat4 prevViewProj{1.f};
        Vec3 cameraPos{0.f};
        u32 brickCount{0};
        Vec3 worldMin{0.f};
        float brickSize{16.f};
        u32 maxDrawDistance{10000};
        u32 enableHiZ{0};
        u64 paletteAddr{0};
        u64 sunAddr{0};
        u64 giAddr{0};
        u32 giEnabled{0};
        u32 shadowsEnabled{0};
        u32 debugEnabled{0};
        u32 useBackface{1};
        u32 useFrustum{1};
        u32 virtualDim{64};
        u32 cutoutPass{0};
    };

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

    struct VoxelGiPushConstants_t {
        u64 brickBufferAddr{0};
        u64 headerAddr{0};
        u64 cascadeAddr{0};
        u64 cascadeDstAddr{0};
        u64 sunAddr{0};
        u32 brickCount{0};
        u32 cascadeRes{64};
        u32 cascadeCount{1};
        float brickSize{16.f};
        u32 _pad0{0};
        u32 _pad1{0};
        Vec3 worldMin{0.f};
    };
    static_assert(sizeof(VoxelGiPushConstants_t) >= 76,
                  "GI push constants must cover the shader block [0,76)");

    static inline u32 PackVoxelVertex(u32 x, u32 y, u32 z, u32 n, u32 ao, u32 mat) {
        return (x & 0x3Fu) | ((y & 0x3Fu) << 6u) | ((z & 0x3Fu) << 12u) |
               ((n & 0x7u) << 18u) | ((ao & 0x3u) << 21u) | ((mat & 0x1FFu) << 23u);
    }
}
