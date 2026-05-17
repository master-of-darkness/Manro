#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/InputBackend.h>

namespace Manro {
    UserCmd_t CInputManager::Poll() const {
        UserCmd_t cmd{};
        if (m_ActionMap)
            m_ActionMap->BuildUserCmd(cmd);
        return cmd;
    }

    bool CInputManager::IsKeyDown(Key k) const {
        if (!m_Backend) return false;
        return m_Backend->IsKeyDown(k);
    }

    bool CInputManager::IsMouseButtonDown(MouseButton button) const {
        if (!m_Backend) return false;
        return m_Backend->IsMouseButtonDown(button);
    }

    RawMouseDelta_t CInputManager::ConsumeMouseDelta() const {
        if (!m_Backend) return {};
        return m_Backend->ConsumeMouseDelta();
    }

    void CInputManager::ProcessEvent(const PlatformEvent_t &event) const {
        if (m_Backend)
            m_Backend->ProcessEvent(event);
    }
} // namespace Manro