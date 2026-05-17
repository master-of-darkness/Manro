#pragma once

#include <Manro/Input/InputAction.h>
#include <Manro/Platform/PlatformEvent.h>
#include <array>

namespace Manro {
    class CInputBackend final {
    public:
        CInputBackend() = default;

        ~CInputBackend() = default;

        void ProcessEvent(const PlatformEvent_t &event);

        bool IsKeyDown(Key k) const;

        bool IsMouseButtonDown(MouseButton button) const;

        RawMouseDelta_t ConsumeMouseDelta();

        float GetGamepadAxis(int axis) const;

        bool IsGamepadButtonDown(int btn) const;

    private:
        std::array<bool, static_cast<size_t>(Key::_Count)> m_bKeyDown{};
        std::array<bool, static_cast<size_t>(MouseButton::_Count)> m_bMouseButtons{};
        RawMouseDelta_t m_MouseDelta{};

        float m_flGamepadAxes[6]{};
        u32 m_unGamepadButtons{0};

        static Key SdlScancodeToKey(int scancode);
    };
} // namespace Manro
