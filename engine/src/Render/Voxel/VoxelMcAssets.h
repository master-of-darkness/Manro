#pragma once

// VoxelMcAssets: vanilla asset pipeline for the voxel renderer.
// Reads a vanilla client layout (textures/block/*.png, blockstates/*.json,
// models/block/*.json — e.g. extracted from the 26.2 client jar) and builds:
// - a deduplicated 16x16 RGBA tile list (texture array layers), and
// - a per-protocol-state face table (tile per cube face) + render flags.
//
// State enumeration + occlusion data come from mcs's committed
// block_states.inc (header-only, no mcs link needed). Mojang IP is never
// committed: the assets dir is user-local (default .mcassets/, override with
// the MC_ASSETS_DIR environment variable).

#include <Manro/Core/Types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Manro {
    // Face order everywhere here: +X, -X, +Y, -Y, +Z, -Z (matches kFaceNormal).
    struct McFaceTiles_t {
        u16 tile[6]{0, 0, 0, 0, 0, 0};
    };

    struct McStateLook_t {
        McFaceTiles_t faces{};
        // Bit flags (see kMcFlag* below).
        u32 flags{0};
    };

    inline constexpr u32 kMcFlagOpaque = 1u << 0u; // full-cube occluder (occlusion==1)
    inline constexpr u32 kMcFlagCutout = 1u << 1u; // alpha-tested (glass/leaves/ice/...)
    inline constexpr u32 kMcFlagSkip = 1u << 2u; // never emit, never occlude (air/flora/tech)
    inline constexpr u32 kMcFlagInner = 1u << 3u; // same-state faces emit (leaves)

    struct McAssetPack_t {
        // RGBA 16x16 pixels per tile, row-major, sRGB bytes. tiles[0] is the
        // missing-texture magenta debug tile.
        std::vector<std::vector<u8> > tiles;
        // Vanilla texture path per tile (parallel to tiles; [0] = "").
        std::vector<std::string> tilePaths;
        // Indexed by protocol block state id (size = maxState + 1).
        std::vector<McStateLook_t> states;
        u32 maxState{0};
        // Diagnostics.
        u32 mappedStates{0};
        u32 fallbackStates{0};
        u32 skippedStates{0};
    };

    // Builds the pack. assetsDir/.../textures/block/<name>.png etc. must
    // exist; assetsDir itself is the "<...>/assets/minecraft" directory.
    // Returns false + err on fatal errors (missing dirs, no states).
    [[nodiscard]] bool BuildMcAssetPack(const std::string &assetsDir, McAssetPack_t &out,
                                        std::string &err);
} // namespace Manro
