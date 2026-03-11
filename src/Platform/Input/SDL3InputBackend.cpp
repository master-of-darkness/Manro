#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <SDL3/SDL.h>
#include <Manro/Core/Logger.h>

namespace Engine {
    Key SDL3InputBackend::SdlScancodeToKey(int sc) {
        switch (static_cast<SDL_Scancode>(sc)) {
            case SDL_SCANCODE_W: return Key::W;
            case SDL_SCANCODE_A: return Key::A;
            case SDL_SCANCODE_S: return Key::S;
            case SDL_SCANCODE_D: return Key::D;
            case SDL_SCANCODE_Q: return Key::Q;
            case SDL_SCANCODE_E: return Key::E;
            case SDL_SCANCODE_SPACE: return Key::Space;
            case SDL_SCANCODE_LSHIFT: return Key::LeftShift;
            case SDL_SCANCODE_LCTRL: return Key::LeftCtrl;
            case SDL_SCANCODE_ESCAPE: return Key::Escape;
            case SDL_SCANCODE_TAB: return Key::Tab;
            case SDL_SCANCODE_RETURN: return Key::Enter;
            case SDL_SCANCODE_F1: return Key::F1;
            case SDL_SCANCODE_F2: return Key::F2;
            case SDL_SCANCODE_F3: return Key::F3;
            case SDL_SCANCODE_F4: return Key::F4;
            case SDL_SCANCODE_UP: return Key::Up;
            case SDL_SCANCODE_DOWN: return Key::Down;
            case SDL_SCANCODE_LEFT: return Key::Left;
            case SDL_SCANCODE_RIGHT: return Key::Right;
            default: return Key::Unknown;
        }
    }

    void SDL3InputBackend::ProcessEvent(const SDL_Event &event) {
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                Key k = SdlScancodeToKey(event.key.scancode);
                if (k != Key::Unknown)
                    m_KeyDown[static_cast<size_t>(k)] = (event.type == SDL_EVENT_KEY_DOWN);
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
                m_MouseDelta.x += event.motion.xrel;
                m_MouseDelta.y += event.motion.yrel;
                break;

            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                int axis = event.gaxis.axis;
                if (axis >= 0 && axis < 6) {
                    m_GamepadAxes[axis] =
                            static_cast<float>(event.gaxis.value) / 32767.f;
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                m_GamepadButtons |= (1u << event.gbutton.button);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                m_GamepadButtons &= ~(1u << event.gbutton.button);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                LOG_INFO("[SDL3InputBackend] Gamepad connected (id {})", event.gdevice.which);
                SDL_OpenGamepad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                LOG_INFO("[SDL3InputBackend] Gamepad disconnected");
                break;

            default:
                break;
        }
    }

    void SDL3InputBackend::OnFrameEnd() {

    }

    bool SDL3InputBackend::IsKeyDown(Key k) const {
        auto idx = static_cast<size_t>(k);
        return idx < m_KeyDown.size() && m_KeyDown[idx];
    }

    RawMouseDelta SDL3InputBackend::ConsumeMouseDelta() {
        RawMouseDelta d = m_MouseDelta;
        m_MouseDelta = {};
        return d;
    }

    float SDL3InputBackend::GetGamepadAxis(int axis) const {
        if (axis < 0 || axis >= 6) return 0.f;
        return m_GamepadAxes[axis];
    }

    bool SDL3InputBackend::IsGamepadButtonDown(int btn) const {
        if (btn < 0 || btn >= 32) return false;
        return (m_GamepadButtons >> btn) & 1u;
    }
} // namespace Engine
