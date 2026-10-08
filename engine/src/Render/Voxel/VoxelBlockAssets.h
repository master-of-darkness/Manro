#pragma once

// VoxelBlockAssets: save-driven block tile pipeline for the voxel renderer.
// Enumerates every distinct block state in the Anvil save (see
// VoxelAnvilReader::ScanSaveStates) and resolves each through the
// blockstates/models/textures unpacked from the Mojang client jar at build
// time (see CMake MANRO_MC_CLIENT_JAR_URL; assets are downloaded, never
// committed). Output:
// - a deduplicated 16x16 RGBA tile list (texture array layers), and
// - a per-state face table (tile per cube face) + render flags, plus a
//   name+properties -> state id resolver used for world fills.
// assetsDir is "<...>/assets/minecraft" (contains textures/block/*.png,
// blockstates/*.json, models/block/*.json); defaults to the build-time
// download, overridable via the --assets-dir start param.

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

    inline constexpr u32 kBlockFlagOpaque = 1u << 0u; // full-cube occluder
    inline constexpr u32 kBlockFlagCutout = 1u << 1u; // alpha-tested (leaves/glass)
    inline constexpr u32 kBlockFlagSkip = 1u << 2u; // never emit, never occlude (air/flora/tech)
    inline constexpr u32 kBlockFlagInner = 1u << 3u; // same-state faces emit (leaves)

    inline constexpr u32 kBlockAir = 0u;
    // Table sizing: any uint16 state indexes safely (real saves use a few
    // hundred to a few thousand states; unused entries read as skip).
    inline constexpr u32 kBlockStateMax = 32768u;

    struct BlockAssetPack_t {
        // RGBA 16x16 pixels per tile, row-major, sRGB bytes. tiles[0] is the
        // missing-texture magenta debug tile.
        std::vector<std::vector<u8> > tiles;
        // Block texture path per tile (parallel to tiles; [0] = "").
        std::vector<std::string> tilePaths;
        // Indexed by block state id (size = kBlockStateMax).
        std::vector<BlockStateLook_t> states;
        // Parallel to states: fluid flag (swimmable, not solid).
        std::vector<u8> fluid;
        u32 maxState{0};
        // State id placed by right-click (oak planks).
        u32 placeState{1};
        // Fallback id for states missing from the resolver (solid stone look).
        u32 fallbackState{1};
        // name|k=v;... (see MakeStateKey) -> state id.
        std::unordered_map<std::string, u32> stateByKey;
        // Reverse lookup: state id -> canonical key (size = maxState+1).
        // Lets gameplay resolve an oriented variant (facing/half/...) of a
        // picked state at placement time.
        std::vector<std::string> stateKeys;
        // Diagnostics.
        u32 mappedStates{0};
        u32 fallbackStates{0};
        u32 skippedStates{0};
    };

    [[nodiscard]] bool BuildBlockAssetPack(const std::string &assetsDir, const std::string &worldDir,
                                          BlockAssetPack_t &out, std::string &err);
} // namespace Manro
