#pragma once

// VoxelAnvilReader: minimal Anvil save reader for the voxel renderer.
// Self-contained: region files + NBT + paletted chunk sections, zlib only.
// No third-party save libs. Sections are returned with their full palette
// (block name + properties); id assignment lives in VoxelBlockAssets.

#include <Manro/Core/Types.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Manro {
    // One palette entry: full block name + properties sorted by key.
    struct FullState {
        std::string name; // e.g. "minecraft:oak_log"
        std::vector<std::pair<std::string, std::string>> props;
    };

    // One 16^3 section: palette indices + palette.
    // pal[] is in brick order (lx + ly*16 + lz*256), matching the voxel
    // brick layout — the packed Y-major order never leaks out.
    struct AnvilSectionStates {
        u16 pal[4096]{0};
        std::vector<FullState> palette;
        bool present{false};
    };

    // Canonical map key: "name|k=v;k=v;..." with props sorted by key.
    inline std::string MakeStateKey(
        const std::string &name, const std::vector<std::pair<std::string, std::string>> &props) {
        std::string key;
        key.reserve(name.size() + props.size() * 16u);
        key += name;
        key += '|';
        for (const auto &p : props) {
            key += p.first;
            key += '=';
            key += p.second;
            key += ';';
        }
        return key;
    }

    class CAnvilWorldReader {
    public:
        CAnvilWorldReader() = default;
        ~CAnvilWorldReader();

        CAnvilWorldReader(const CAnvilWorldReader &) = delete;
        CAnvilWorldReader &operator=(const CAnvilWorldReader &) = delete;

        // Locates region data under worldDir (both "<world>/region" and
        // "<world>/dimensions/minecraft/overworld/region" layouts).
        // Returns true when at least one *.mca file exists.
        bool Open(const std::string &worldDir);

        [[nodiscard]] bool IsOpen() const { return m_bOpen; }

        // Reads one section (chunk sx,sz + section sy): palette indices plus
        // the section palette. False (present=false) for missing data.
        bool ReadSectionStates(int sx, int sy, int sz, AnvilSectionStates &out);

        // Collects every distinct palette entry in the save (sorted by key).
        // Used to enumerate block states up front (see VoxelBlockAssets).
        bool ScanSaveStates(std::vector<FullState> &uniqueOut);

        // Spawn from level.dat (Data/SpawnX/Y/Z or Data/spawn/pos).
        // False when unreadable.
        bool ReadSpawn(Vec3 &spawn) const;

        [[nodiscard]] const std::string &GetRegionDir() const { return m_RegionDir; }

    private:
        struct Impl;
        Impl *m_Impl{nullptr};
        std::string m_WorldDir;
        std::string m_RegionDir;
        bool m_bOpen{false};
    };
} // namespace Manro
