#pragma once

#include "InputAction.h"

union SDL_Event;

namespace Engine {
    class IInputBackend;

    class InputManager {
    public:
        InputManager() = default;

        ~InputManager() = default;

        InputManager(const InputManager &) = delete;

        InputManager &operator=(const InputManager &) = delete;

        void SetBackend(IInputBackend *backend) { m_Backend = backend; }
        void SetActionMap(IInputActionMap *map) { m_ActionMap = map; }

        UserCmd Poll();

        bool IsKeyDown(Key k) const;

        RawMouseDelta ConsumeMouseDelta();

        void ProcessSDLEvent(const SDL_Event &event);

    private:
        IInputBackend *m_Backend{nullptr};
        IInputActionMap *m_ActionMap{nullptr};
    };
} // namespace Engine