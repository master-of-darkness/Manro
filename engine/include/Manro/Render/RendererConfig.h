#pragma once

#include <Manro/Core/Types.h>
namespace Manro {
    struct RendererConfig_t {
        u32 maxFramesInFlight = 3;

        /// Maximum number of instance transforms that can be batched per frame.
        u32 maxInstances = 65536;

        /// Maximum number of lights supported in the scene.
        u32 maxLights = 1024;

        /// Maximum number of lights that can affect a single tile.
        u32 maxLightsPerTile = 64;

        /// Tile size in pixels for clustered/tiled light culling.
        u32 tileSize = 16;

        /// Resolution of shadow maps (width and height).
        u32 shadowMapSize = 2048;

        /// Number of shadow cascade levels for directional lights.
        u32 shadowCascades = 4;

        /// Enable VSync by default.
        bool vsync = true;

        /// Enable triple buffering (3 swapchain images).
        bool tripleBuffering = true;

        /// Returns default configuration suitable for most desktop GPUs.
        static RendererConfig_t Default() {
            return RendererConfig_t{};
        }
    };
} // namespace Manro