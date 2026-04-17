#include <Manro/Platform/Input/InputBackend.h>
#include <SDL3/SDL.h>
#include <Manro/Core/Logger.h>

namespace Manro {
    Key InputBackend::SdlScancodeToKey(int sc) {
        switch (static_cast<SDL_Scancode>(sc)) {
            case SDL_SCANCODE_W:
                return Key::W;
            case SDL_SCANCODE_A:
                return Key::A;
            case SDL_SCANCODE_S:
                return Key::S;
            case SDL_SCANCODE_D:
                return Key::D;
            case SDL_SCANCODE_Q:
                return Key::Q;
            case SDL_SCANCODE_E:
                return Key::E;
            case SDL_SCANCODE_1:
                return Key::Num1;
            case SDL_SCANCODE_2:
                return Key::Num2;
            case SDL_SCANCODE_3:
                return Key::Num3;
            case SDL_SCANCODE_4:
                return Key::Num4;
            case SDL_SCANCODE_5:
                return Key::Num5;
            case SDL_SCANCODE_6:
                return Key::Num6;
            case SDL_SCANCODE_7:
                return Key::Num7;
            case SDL_SCANCODE_8:
                return Key::Num8;
            case SDL_SCANCODE_9:
                return Key::Num9;
            case SDL_SCANCODE_SPACE:
                return Key::Space;
            case SDL_SCANCODE_LSHIFT:
                return Key::LeftShift;
            case SDL_SCANCODE_LCTRL:
                return Key::LeftCtrl;
            case SDL_SCANCODE_ESCAPE:
                return Key::Escape;
            case SDL_SCANCODE_TAB:
                return Key::Tab;
            case SDL_SCANCODE_RETURN:
                return Key::Enter;
            case SDL_SCANCODE_F1:
                return Key::F1;
            case SDL_SCANCODE_F2:
                return Key::F2;
            case SDL_SCANCODE_F3:
                return Key::F3;
            case SDL_SCANCODE_F4:
                return Key::F4;
            case SDL_SCANCODE_F11:
                return Key::F11;
            case SDL_SCANCODE_UP:
                return Key::Up;
            case SDL_SCANCODE_DOWN:
                return Key::Down;
            case SDL_SCANCODE_LEFT:
                return Key::Left;
            case SDL_SCANCODE_RIGHT:
                return Key::Right;
            default:
                return Key::Unknown;
        }
    }

    void InputBackend::ProcessEvent(const PlatformEvent &platformEvent) {
        const SDL_Event &event = *static_cast<const SDL_Event *>(platformEvent.data);
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

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                MouseButton button = MouseButton::_Count;
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        button = MouseButton::Left;
                        break;
                    case SDL_BUTTON_RIGHT:
                        button = MouseButton::Right;
                        break;
                    case SDL_BUTTON_MIDDLE:
                        button = MouseButton::Middle;
                        break;
                    default:
                        break;
                }

                const auto idx = static_cast<size_t>(button);
                if (idx < m_MouseButtons.size())
                    m_MouseButtons[idx] = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                break;
            }

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
                LOG_INFO("[InputBackend] Gamepad connected (id {})", event.gdevice.which);
                SDL_OpenGamepad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                LOG_INFO("[InputBackend] Gamepad disconnected");
                break;

            default:
                break;
        }
    }

    bool InputBackend::IsKeyDown(Key k) const {
        auto idx = static_cast<size_t>(k);
        return idx < m_KeyDown.size() && m_KeyDown[idx];
    }

    bool InputBackend::IsMouseButtonDown(MouseButton button) const {
        const auto idx = static_cast<size_t>(button);
        return idx < m_MouseButtons.size() && m_MouseButtons[idx];
    }

    RawMouseDelta InputBackend::ConsumeMouseDelta() {
        RawMouseDelta d = m_MouseDelta;
        m_MouseDelta = {};
        return d;
    }

    float InputBackend::GetGamepadAxis(int axis) const {
        if (axis < 0 || axis >= 6) return 0.f;
        return m_GamepadAxes[axis];
    }

    bool InputBackend::IsGamepadButtonDown(int btn) const {
        if (btn < 0 || btn >= 32) return false;
        return (m_GamepadButtons >> btn) & 1u;
    }
} // namespace Manro