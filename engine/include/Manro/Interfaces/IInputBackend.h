#pragma once

#include <Manro/Input/InputAction.h>
#include <Manro/Interfaces/Interface.h>
#include <Manro/Platform/PlatformEvent.h>

namespace Manro {
    class IInputBackend : public Interface {
    public:
        ~IInputBackend() override = default;

        virtual void ProcessEvent(const PlatformEvent &event) = 0;

        virtual bool IsKeyDown(Key k) const = 0;

        virtual bool IsMouseButtonDown(MouseButton button) const = 0;

        virtual RawMouseDelta ConsumeMouseDelta() = 0;
    };
} // namespace Manro