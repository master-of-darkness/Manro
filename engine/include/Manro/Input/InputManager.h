#pragma once

#include <Manro/Input/InputAction.h>
#include <Manro/Platform/PlatformEvent.h>

namespace Manro {
    class IInputBackend;

    class InputManager {
    public:
        InputManager() = default;

        ~InputManager() = default;

        InputManager(const InputManager &) = delete;

        InputManager &operator=(const InputManager &) = delete;

        void SetBackend(IInputBackend *backend) { m_Backend = backend; }

        UserCmd Poll();

        bool IsKeyDown(Key k) const;

        bool IsMouseButtonDown(MouseButton button) const;

        RawMouseDelta ConsumeMouseDelta();

        void ProcessEvent(const PlatformEvent &event);

    private:
        IInputBackend *m_Backend{nullptr};
        IInputActionMap *m_ActionMap{nullptr};
    };
} // namespace Manro