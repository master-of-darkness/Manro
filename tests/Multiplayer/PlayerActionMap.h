#pragma once

#include <Manro/Input/InputAction.h>
#include <Manro/Input/InputManager.h>
#include <algorithm>
#include <cmath>

class PlayerActionMap final : public Engine::IInputActionMap {
public:
    struct Settings {
        float MouseSensitivity{0.1f};
        float PitchMin{-89.f};
        float PitchMax{89.f};

        Settings() = default;
    };

    explicit PlayerActionMap(Engine::InputManager &inputMgr)
        : PlayerActionMap(inputMgr, Settings{}) {
    }

    explicit PlayerActionMap(Engine::InputManager &inputMgr, Settings s)
        : m_Input(inputMgr), m_Settings(s) {
    }

    void BuildUserCmd(Engine::UserCmd &cmd) override {
        using K = Engine::Key;

        float fwd = (m_Input.IsKeyDown(K::W) ? 1.f : 0.f) - (m_Input.IsKeyDown(K::S) ? 1.f : 0.f);
        float right = (m_Input.IsKeyDown(K::D) ? 1.f : 0.f) - (m_Input.IsKeyDown(K::A) ? 1.f : 0.f);
        float len = std::sqrtf(fwd * fwd + right * right);
        if (len > 0.001f) {
            fwd /= len;
            right /= len;
        }

        cmd.MoveForward = fwd;
        cmd.MoveRight = right;

        Engine::RawMouseDelta delta = m_Input.ConsumeMouseDelta();
        m_Yaw += delta.x * m_Settings.MouseSensitivity;
        m_Pitch = std::clamp(m_Pitch - delta.y * m_Settings.MouseSensitivity,
                             m_Settings.PitchMin, m_Settings.PitchMax);
        cmd.ViewYaw = m_Yaw;
        cmd.ViewPitch = m_Pitch;

        // cmd.SetButton(Engine::ButtonBit::Jump, m_Input.IsKeyDown(K::Space)); TODO: fix me
        cmd.SetButton(Engine::ButtonBit::Crouch, m_Input.IsKeyDown(K::LeftShift));
    }

    void OnFocusLost() override {
        /* could reset held-key state here */
    }

    float GetYaw() const { return m_Yaw; }
    float GetPitch() const { return m_Pitch; }

private:
    Engine::InputManager &m_Input;
    Settings m_Settings;
    float m_Yaw{-90.f};
    float m_Pitch{-10.f};
};
