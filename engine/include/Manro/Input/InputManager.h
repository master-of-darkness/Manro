#pragma once

#include <Manro/Input/InputAction.h>
#include <Manro/Platform/PlatformEvent.h>

namespace Manro {
    class CInputBackend;

    class CInputManager {
    public:
        CInputManager() = default;

        ~CInputManager() = default;

        CInputManager(const CInputManager &) = delete;

        CInputManager &operator=(const CInputManager &) = delete;

        void SetBackend(CInputBackend *backend) { m_Backend = backend; }

        void SetActionMap(IInputActionMap *actionMap) { m_ActionMap = actionMap; }

        [[nodiscard]] UserCmd_t Poll() const;

        [[nodiscard]] bool IsKeyDown(Key k) const;

        [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const;

        RawMouseDelta_t ConsumeMouseDelta() const;

        void ProcessEvent(const PlatformEvent_t &event) const;

    private:
        CInputBackend *m_Backend{nullptr};
        IInputActionMap *m_ActionMap{nullptr};
    };
} // namespace Manro