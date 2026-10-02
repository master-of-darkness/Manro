#pragma once

// CVoxelStreamWorld: save-backed section source for the voxel renderer.
// Sections are 16^3 blocks (== our bricks). Y spans -64..319 (sections -4..19).
// World data comes from an Anvil save on disk (region files under worldDir,
// read with the built-in reader — no third-party save libs). There is no
// procedural fallback: without a save the volume fills as air.
//
// Streaming world: sections allocate/fill around the player (Update center)
// and evict past a hysteresis ring, reusing resident brick indices (no
// unbind cost). Page-table slots wrap mod-64 against the fixed Init origin
// (shaders do the same), so the world is unbounded in XZ.

#include <Manro/Core/Types.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Manro {
    class CVoxelWorld;
    struct BlockAssetPack_t;

    struct VoxelStreamDesc_t {
        // Save directory holding region files (required; no fallback).
        std::string worldDir;
        // Section radius around spawn in X/Z (full height -4..19 always).
        // R=6 -> 13*24*13 = 4056 bricks (cap 8192).
        int radiusSections{6};
        // Max sections filled per Update call. 12 keeps streaming hitches
        // small while filling R=6.
        int fillBudgetPerUpdate{12};
    };

    class CVoxelStreamWorld {
    public:
        CVoxelStreamWorld();
        ~CVoxelStreamWorld();

        CVoxelStreamWorld(const CVoxelStreamWorld &) = delete;
        CVoxelStreamWorld &operator=(const CVoxelStreamWorld &) = delete;

        // Allocates + fills the section volume. Returns the spawn position
        // (level.dat spawn when available, else a default above y=80).
        Vec3 Init(CVoxelWorld &world, const BlockAssetPack_t &pack, const VoxelStreamDesc_t &desc);

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
        // Block state id placed by right-click (planks).
        [[nodiscard]] i32 PlaceState() const;

        [[nodiscard]] int UnfilledCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Manro
