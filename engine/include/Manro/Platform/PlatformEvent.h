#pragma once

namespace Manro {
    /// Opaque wrapper around a platform-specific event (e.g. SDL_Event).
    /// Consumers that need the concrete type must cast \c data themselves after
    /// including the relevant platform header.
    struct PlatformEvent {
        const void *data{nullptr};
    };
} // namespace Manro
