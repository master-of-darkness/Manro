#include <Manro/Platform/Input/InputBackend.h>
#include <Manro/Core/InterfaceReg.h>
#include <SDL3/SDL.h>
#include <Manro/Core/Logger.h>

namespace Manro {
    Key CInputBackend::SdlScancodeToKey(int sc) {
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

    void CInputBackend::ProcessEvent(const PlatformEvent_t &platformEvent) {
        const SDL_Event &event = *static_cast<const SDL_Event *>(platformEvent.data);
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                Key k = SdlScancodeToKey(event.key.scancode);
                if (k != Key::Unknown)
                    m_bKeyDown[static_cast<size_t>(k)] = (event.type == SDL_EVENT_KEY_DOWN);
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
                if (idx < m_bMouseButtons.size())
                    m_bMouseButtons[idx] = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                break;
            }

            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                int axis = event.gaxis.axis;
                if (axis >= 0 && axis < 6) {
                    m_flGamepadAxes[axis] =
                            static_cast<float>(event.gaxis.value) / 32767.f;
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                m_unGamepadButtons |= (1u << event.gbutton.button);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                m_unGamepadButtons &= ~(1u << event.gbutton.button);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                LOG_INFO("[CInputBackend] Gamepad connected (id {})", event.gdevice.which);
                SDL_OpenGamepad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                LOG_INFO("[CInputBackend] Gamepad disconnected");
                break;

            default:
                break;
        }
    }

    bool CInputBackend::IsKeyDown(Key k) const {
        auto idx = static_cast<size_t>(k);
        return idx < m_bKeyDown.size() && m_bKeyDown[idx];
    }

    bool CInputBackend::IsMouseButtonDown(MouseButton button) const {
        const auto idx = static_cast<size_t>(button);
        return idx < m_bMouseButtons.size() && m_bMouseButtons[idx];
    }

    RawMouseDelta_t CInputBackend::ConsumeMouseDelta() {
        RawMouseDelta_t d = m_MouseDelta;
        m_MouseDelta = {};
        return d;
    }

    float CInputBackend::GetGamepadAxis(int axis) const {
        if (axis < 0 || axis >= 6) return 0.f;
        return m_flGamepadAxes[axis];
    }

    bool CInputBackend::IsGamepadButtonDown(int btn) const {
        if (btn < 0 || btn >= 32) return false;
        return (m_unGamepadButtons >> btn) & 1u;
    }

    EXPOSE_SINGLE_INTERFACE(CInputBackend, IInputBackend, "IINPUTBACKEND_001")

    void ForceLinkInputBackend() {
    }
} // namespace Manro
