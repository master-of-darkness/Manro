#pragma once

#include <Manro/Core/Handle.h>

namespace Manro {
    struct MeshTag {
    };

    using MeshHandle = Handle<MeshTag>;
    inline constexpr MeshHandle kInvalidMesh{};

    struct TextureTag {
    };

    using TextureHandle = Handle<TextureTag>;
    inline constexpr TextureHandle kInvalidTexture{};

    struct SoundTag {
    };

    using SoundHandle = Handle<SoundTag>;
    inline constexpr SoundHandle kInvalidSound{};

    struct WindowTag {
    };

    using WindowHandle = Handle<WindowTag>;
    inline constexpr WindowHandle kInvalidWindow{};

    struct PhysicsBodyTag {
    };

    using PhysicsBodyHandle = Handle<PhysicsBodyTag>;
    inline constexpr PhysicsBodyHandle kInvalidBodyHandle{};
} // namespace Manro