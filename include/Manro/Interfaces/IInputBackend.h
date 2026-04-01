#pragma once

#include <Manro/Input/InputAction.h>
#include <Manro/Interfaces/Interface.h>

union SDL_Event;

namespace Manro {
    class IInputBackend : public Interface {
    public:
        virtual ~IInputBackend() = default;

        virtual void ProcessEvent(const SDL_Event &event) = 0;

        virtual bool IsKeyDown(Key k) const = 0;

        virtual RawMouseDelta ConsumeMouseDelta() = 0;
    };
} // namespace Manro