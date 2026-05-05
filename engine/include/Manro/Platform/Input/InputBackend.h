#pragma once

#include <Manro/Interfaces/IInputBackend.h>
#include <Manro/Input/InputAction.h>
#include <array>

namespace Manro {
    class CInputBackend final : public IInputBackend {
    public:
        CInputBackend() = default;

        ~CInputBackend() override = default;

        bool Connect(void * (*factory)(const char *, int *)) override { return true; }

        void Disconnect() override {
        }

        InitReturnVal_t Init() override { return INIT_OK; }

        void Shutdown() override {
        }

        void ProcessEvent(const PlatformEvent_t &event) override;

        bool IsKeyDown(Key k) const override;

        bool IsMouseButtonDown(MouseButton button) const override;

        RawMouseDelta_t ConsumeMouseDelta() override;

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
