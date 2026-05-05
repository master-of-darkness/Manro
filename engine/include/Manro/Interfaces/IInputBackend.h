#pragma once

#include <Manro/Input/InputAction.h>
#include <Manro/Interfaces/IAppSystem.h>
#include <Manro/Platform/PlatformEvent.h>

namespace Manro {
    class IInputBackend : public IAppSystem {
    public:
        ~IInputBackend() override = default;

        virtual void ProcessEvent(const PlatformEvent_t &event) = 0;

        [[nodiscard]] virtual bool IsKeyDown(Key k) const = 0;

        [[nodiscard]] virtual bool IsMouseButtonDown(MouseButton button) const = 0;

        virtual RawMouseDelta_t ConsumeMouseDelta() = 0;
    };
} // namespace Manro