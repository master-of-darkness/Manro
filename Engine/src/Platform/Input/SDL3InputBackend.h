#pragma once

#include "IInputBackend.h"
#include <Input/InputAction.h>
#include <array>

union SDL_Event;

namespace Engine {
    class SDL3InputBackend final : public IInputBackend {
    public:
        void ProcessEvent(const SDL_Event &event) override;

        void OnFrameEnd() override;

        bool IsKeyDown(Key k) const;

        RawMouseDelta ConsumeMouseDelta();

        float GetGamepadAxis(int axis) const;

        bool IsGamepadButtonDown(int btn) const;

    private:
        std::array<bool, static_cast<size_t>(Key::_Count)> m_KeyDown{};
        RawMouseDelta m_MouseDelta{};

        float m_GamepadAxes[6]{};
        u32 m_GamepadButtons{0};

        static Key SdlScancodeToKey(int scancode);
    };
} // namespace Engine
