#pragma once

// CVoxelMcWorld: Minecraft-dimension world source for the voxel renderer.
// Sections are 16^3 blocks (== our bricks). Y spans -64..319 (sections -4..19).
// Data comes from an mcs AnvilWorld when region files exist, otherwise from
// an MC-proportioned procedural fallback (sea level, strata, trees), so the
// renderer runs with or without a save on disk.
//
// Streaming world: sections allocate/fill around the player (Update center)
// and evict past a hysteresis ring, reusing resident brick indices (no
// unbind cost). Page-table slots wrap mod-64 against the fixed Init origin
// (shaders do the same), so the world is unbounded in XZ. Requires C++23
// (mcs headers).

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

        // Fills up to fillBudget sections around the camera (nearest first),
        // evicting sections past the hysteresis ring as the center moves.
        // Returns remaining unfilled count (0 = caught up).
        // flightSlot is the frame-in-flight index for deferred brick uploads
        // (see CVoxelWorld::StageBrickBatch).
        int Update(CVoxelWorld &world, const Vec3 &cameraPos, u32 flightSlot);

        // Physics/raycast queries in world-block coords. Unfilled or
        // untracked sections read solid (nothing falls through unloaded
        // world); y < -64 reads solid, y >= 320 reads air/non-fluid.
        // Fluids are NOT solid (swimmable); see ApplyEdit.
        [[nodiscard]] bool IsSolidAt(i64 x, i64 y, i64 z) const;
        [[nodiscard]] bool IsFluidAt(i64 x, i64 y, i64 z) const;
        // Gameplay-edit mirror maintenance: the GPU edit compute mutates
        // brick occupancy in VRAM, but physics/raycasts read the CPU mirrors
        // below. Without this, broken blocks keep ghost collision (+ block
        // the raycast) and placed blocks have none. Home-brick only, mirroring
        // the GPU edit shader (which touches just the brick containing pos;
        // radius < 0.5 = the single voxel at floor(pos)). No-op when the home
        // section isn't filled (the GPU edit no-ops there too).
        // op: 0 = erase (clears occ+fluid), 1 = write (sets occ, clears fluid).
        void ApplyEdit(const Vec3 &pos, float radius, u32 op);
        // Protocol state id placed by right-click (planks, stone fallback).
        [[nodiscard]] i32 PlaceState() const;

        [[nodiscard]] bool HasAnvil() const;
        [[nodiscard]] int UnfilledCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Manro
