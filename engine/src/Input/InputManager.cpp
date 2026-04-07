#include <Manro/Input/InputManager.h>
#include <Manro/Interfaces/IInputBackend.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>

namespace Manro {
    UserCmd InputManager::Poll() {
        UserCmd cmd{};
        if (m_ActionMap)
            m_ActionMap->BuildUserCmd(cmd);
        return cmd;
    }

    bool InputManager::IsKeyDown(Key k) const {
        if (!m_Backend) return false;
        return m_Backend->IsKeyDown(k);
    }

    bool InputManager::IsMouseButtonDown(MouseButton button) const {
        if (!m_Backend) return false;
        return m_Backend->IsMouseButtonDown(button);
    }

    RawMouseDelta InputManager::ConsumeMouseDelta() {
        if (!m_Backend) return {};
        return m_Backend->ConsumeMouseDelta();
    }

    void InputManager::ProcessEvent(const PlatformEvent &event) {
        if (m_Backend)
            m_Backend->ProcessEvent(event);
    }
} // namespace Manro