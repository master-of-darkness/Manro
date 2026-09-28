#pragma once

// CVoxelMcWorld: Minecraft-dimension world source for the voxel renderer.
// Sections are 16^3 blocks (== our bricks). Y spans -64..319 (sections -4..19).
// Data comes from an mcs AnvilWorld when region files exist, otherwise from
// an MC-proportioned procedural fallback (sea level, strata, trees), so the
// renderer runs with or without a save on disk.
//
// v1 allocates a fixed section volume around spawn (no eviction paging yet:
// the world has no FreeBrick — streaming that outruns a static volume is a
// follow-up). Requires C++23 (mcs headers).

#include <Manro/Core/Types.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Manro {
    class CVoxelWorld;
    struct McAssetPack_t;

    struct McWorldDesc_t {
        // Empty = procedural fallback only.
        std::string worldDir;
        // Section radius around spawn in X/Z (full height -4..19 always).
        // R=6 -> 13*24*13 = 4056 bricks (cap 8192).
        int radiusSections{6};
        // Max sections filled per Update call. Procedural fill is ~0.2ms /
        // section after column hoisting (Anvil ~1ms); 12 keeps streaming
        // hitches under ~8ms while filling R=6 in ~0.35s.
        int fillBudgetPerUpdate{12};
    };

    class CVoxelMcWorld {
    public:
        CVoxelMcWorld();
        ~CVoxelMcWorld();

        CVoxelMcWorld(const CVoxelMcWorld &) = delete;
        CVoxelMcWorld &operator=(const CVoxelMcWorld &) = delete;

        // Allocates + fills the section volume. Returns the spawn position
        // (Anvil level spawn when available, else above procedural terrain).
        // worldDesc must already be Init'ed with matching worldMin/virtualDim
        // (see SuggestedWorldMin/SuggestedVirtualDim).
        Vec3 Init(CVoxelWorld &world, const McAssetPack_t &pack, const McWorldDesc_t &desc);

        // Fills up to fillBudget sections around the camera (nearest first).
        // Returns remaining unfilled count (0 = complete).
        int Update(CVoxelWorld &world, const Vec3 &cameraPos);

        [[nodiscard]] bool HasAnvil() const;
        [[nodiscard]] int UnfilledCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Manro
