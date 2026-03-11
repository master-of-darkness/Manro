#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/IInputBackend.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>

namespace Engine {
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

    RawMouseDelta InputManager::ConsumeMouseDelta() {
        if (!m_Backend) return {};
        return m_Backend->ConsumeMouseDelta();
    }

    void InputManager::ProcessSDLEvent(const SDL_Event &event) {
        if (m_Backend)
            m_Backend->ProcessEvent(event);
    }
} // namespace Engine
