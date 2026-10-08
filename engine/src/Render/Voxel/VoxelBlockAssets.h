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
        u32 uvPacked[6]{0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u,
                        0xFFFF0000u, 0xFFFF0000u, 0xFFFF0000u};
    };

    inline constexpr u32 kBlockFlagOpaque = 1u << 0u;
    inline constexpr u32 kBlockFlagCutout = 1u << 1u;
    inline constexpr u32 kBlockFlagSkip = 1u << 2u;
    inline constexpr u32 kBlockFlagInner = 1u << 3u;

    inline constexpr u32 kBlockAir = 0u;

    inline constexpr u32 kBlockStateMax = 32768u;

    struct BlockAssetPack_t {

        std::vector<std::vector<u8> > tiles;

        std::vector<std::string> tilePaths;

        std::vector<BlockStateLook_t> states;

        std::vector<u8> fluid;
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
