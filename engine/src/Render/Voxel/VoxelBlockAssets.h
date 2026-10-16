#pragma once

#include <Manro/Core/Types.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Manro {
    struct BlockFaceTiles_t {
        u16 tile[6]{0, 0, 0, 0, 0, 0};
    };

    struct BlockStateLook_t {
        BlockFaceTiles_t faces{};
        u32 flags{0};
        u8 shapeMin[3]{0, 0, 0};
        u8 shapeMax[3]{16, 16, 16};
        float collMin[3]{0.f, 0.f, 0.f};
        float collMax[3]{1.f, 1.f, 1.f};
        u32 uvPacked[6]{0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u,
                        0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u};
    };

    inline constexpr u32 kBlockFlagOpaque = 1u << 0u;
    inline constexpr u32 kBlockFlagCutout = 1u << 1u;
    inline constexpr u32 kBlockFlagSkip = 1u << 2u;
    inline constexpr u32 kBlockFlagInner = 1u << 3u;
    // Explicit per-box geometry path (fences): the task shader expands one
    // voxel-face entry into one face-cache entry per surviving sub-box face,
    // and the mesh shader draws each box with VoxelFaceQuad instead of the
    // single union-AABB quad. Never combined with opaque/cutout.
    inline constexpr u32 kBlockFlagMulti = 1u << 4u;
    inline constexpr u32 kBlockFlagLeaf = 1u << 5u;

    inline constexpr u32 kBlockAir = 0u;

    inline constexpr u32 kBlockStateMax = 32768u;

    // Per-state multi-box table layout (u32 words, must match
    // kVoxelBoxWordsPerState in shaders/voxel_common.slang):
    //   w0: box count (0 = single-path, 2..9 = multi-box fence)
    //   w1: plank tile id (low 16) | internal-face mask bits [0:16) (high 16)
    //   w2: internal-face mask bits [16:48)
    //   w3: internal-face mask bits [48:54) (low 6)
    //   w[4+2*i], w[4+2*i+1]: box i min/max packed (x | y<<8 | z<<16)
    // Mask bit index = box * 6 + face; set = face fully inside another box.
    inline constexpr u32 kBlockBoxWordsPerState = 22u;
    inline constexpr u32 kBlockBoxMax = 9u;

    struct BlockAssetPack_t {

        std::vector<std::vector<u8> > tiles;

        std::vector<std::string> tilePaths;

        std::vector<BlockStateLook_t> states;

        // Multi-box geometry table, sized kBlockStateMax *
        // kBlockBoxWordsPerState. All zeros = single-path for every state.
        std::vector<u32> boxTable;

        std::vector<u8> fluid;
        // Vanilla lighting: per-state block-light emission (0-15) and sky-light
        // opacity (0 transparent, 1 filtering, 15 opaque). Sized kBlockStateMax.
        std::vector<u8> emission;
        std::vector<u8> skyOpacity;
        u32 maxState{0};

        u32 placeState{1};

        u32 fallbackState{1};

        std::unordered_map<std::string, u32> stateByKey;

        std::vector<std::string> stateKeys;

        u32 mappedStates{0};
        u32 fallbackStates{0};
        u32 skippedStates{0};
    };

    [[nodiscard]] bool BuildBlockAssetPack(const std::string &assetsDir, const std::string &worldDir,
                                          BlockAssetPack_t &out, std::string &err);
}
