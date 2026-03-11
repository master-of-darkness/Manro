#pragma once

#include <Manro/Input/InputAction.h>

union SDL_Event;

namespace Engine {
    class IInputBackend {
    public:
        virtual ~IInputBackend() = default;

        virtual void ProcessEvent(const SDL_Event &event) = 0;

        virtual void OnFrameEnd() {
        }

        virtual bool IsKeyDown(Key k) const = 0;

        virtual RawMouseDelta ConsumeMouseDelta() = 0;
    };
} // namespace Engine
