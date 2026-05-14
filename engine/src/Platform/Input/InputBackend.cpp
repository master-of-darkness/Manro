#include <Manro/Platform/Input/InputBackend.h>
#include <SDL3/SDL.h>
#include <Manro/Core/Logger.h>

namespace Manro {
    Key CInputBackend::SdlScancodeToKey(int sc) {
#define MNR_KEY_CASE(scancode, key) case scancode: return Key::key
        switch (static_cast<SDL_Scancode>(sc)) {
            MNR_KEY_CASE(SDL_SCANCODE_A, A);
            MNR_KEY_CASE(SDL_SCANCODE_B, B);
            MNR_KEY_CASE(SDL_SCANCODE_C, C);
            MNR_KEY_CASE(SDL_SCANCODE_D, D);
            MNR_KEY_CASE(SDL_SCANCODE_E, E);
            MNR_KEY_CASE(SDL_SCANCODE_F, F);
            MNR_KEY_CASE(SDL_SCANCODE_G, G);
            MNR_KEY_CASE(SDL_SCANCODE_H, H);
            MNR_KEY_CASE(SDL_SCANCODE_I, I);
            MNR_KEY_CASE(SDL_SCANCODE_J, J);
            MNR_KEY_CASE(SDL_SCANCODE_K, K);
            MNR_KEY_CASE(SDL_SCANCODE_L, L);
            MNR_KEY_CASE(SDL_SCANCODE_M, M);
            MNR_KEY_CASE(SDL_SCANCODE_N, N);
            MNR_KEY_CASE(SDL_SCANCODE_O, O);
            MNR_KEY_CASE(SDL_SCANCODE_P, P);
            MNR_KEY_CASE(SDL_SCANCODE_Q, Q);
            MNR_KEY_CASE(SDL_SCANCODE_R, R);
            MNR_KEY_CASE(SDL_SCANCODE_S, S);
            MNR_KEY_CASE(SDL_SCANCODE_T, T);
            MNR_KEY_CASE(SDL_SCANCODE_U, U);
            MNR_KEY_CASE(SDL_SCANCODE_V, V);
            MNR_KEY_CASE(SDL_SCANCODE_W, W);
            MNR_KEY_CASE(SDL_SCANCODE_X, X);
            MNR_KEY_CASE(SDL_SCANCODE_Y, Y);
            MNR_KEY_CASE(SDL_SCANCODE_Z, Z);

            MNR_KEY_CASE(SDL_SCANCODE_0, Num0);
            MNR_KEY_CASE(SDL_SCANCODE_1, Num1);
            MNR_KEY_CASE(SDL_SCANCODE_2, Num2);
            MNR_KEY_CASE(SDL_SCANCODE_3, Num3);
            MNR_KEY_CASE(SDL_SCANCODE_4, Num4);
            MNR_KEY_CASE(SDL_SCANCODE_5, Num5);
            MNR_KEY_CASE(SDL_SCANCODE_6, Num6);
            MNR_KEY_CASE(SDL_SCANCODE_7, Num7);
            MNR_KEY_CASE(SDL_SCANCODE_8, Num8);
            MNR_KEY_CASE(SDL_SCANCODE_9, Num9);

            MNR_KEY_CASE(SDL_SCANCODE_MINUS, Minus);
            MNR_KEY_CASE(SDL_SCANCODE_EQUALS, Equals);
            MNR_KEY_CASE(SDL_SCANCODE_LEFTBRACKET, LeftBracket);
            MNR_KEY_CASE(SDL_SCANCODE_RIGHTBRACKET, RightBracket);
            MNR_KEY_CASE(SDL_SCANCODE_BACKSLASH, Backslash);
            MNR_KEY_CASE(SDL_SCANCODE_SEMICOLON, Semicolon);
            MNR_KEY_CASE(SDL_SCANCODE_APOSTROPHE, Apostrophe);
            MNR_KEY_CASE(SDL_SCANCODE_GRAVE, Grave);
            MNR_KEY_CASE(SDL_SCANCODE_COMMA, Comma);
            MNR_KEY_CASE(SDL_SCANCODE_PERIOD, Period);
            MNR_KEY_CASE(SDL_SCANCODE_SLASH, Slash);

            MNR_KEY_CASE(SDL_SCANCODE_SPACE, Space);
            MNR_KEY_CASE(SDL_SCANCODE_TAB, Tab);
            MNR_KEY_CASE(SDL_SCANCODE_RETURN, Enter);
            MNR_KEY_CASE(SDL_SCANCODE_BACKSPACE, Backspace);
            MNR_KEY_CASE(SDL_SCANCODE_ESCAPE, Escape);
            MNR_KEY_CASE(SDL_SCANCODE_CAPSLOCK, CapsLock);

            MNR_KEY_CASE(SDL_SCANCODE_LSHIFT, LeftShift);
            MNR_KEY_CASE(SDL_SCANCODE_RSHIFT, RightShift);
            MNR_KEY_CASE(SDL_SCANCODE_LCTRL, LeftCtrl);
            MNR_KEY_CASE(SDL_SCANCODE_RCTRL, RightCtrl);
            MNR_KEY_CASE(SDL_SCANCODE_LALT, LeftAlt);
            MNR_KEY_CASE(SDL_SCANCODE_RALT, RightAlt);
            MNR_KEY_CASE(SDL_SCANCODE_LGUI, LeftSuper);
            MNR_KEY_CASE(SDL_SCANCODE_RGUI, RightSuper);
            MNR_KEY_CASE(SDL_SCANCODE_APPLICATION, Menu);

            MNR_KEY_CASE(SDL_SCANCODE_UP, Up);
            MNR_KEY_CASE(SDL_SCANCODE_DOWN, Down);
            MNR_KEY_CASE(SDL_SCANCODE_LEFT, Left);
            MNR_KEY_CASE(SDL_SCANCODE_RIGHT, Right);
            MNR_KEY_CASE(SDL_SCANCODE_INSERT, Insert);
            MNR_KEY_CASE(SDL_SCANCODE_DELETE, Delete);
            MNR_KEY_CASE(SDL_SCANCODE_HOME, Home);
            MNR_KEY_CASE(SDL_SCANCODE_END, End);
            MNR_KEY_CASE(SDL_SCANCODE_PAGEUP, PageUp);
            MNR_KEY_CASE(SDL_SCANCODE_PAGEDOWN, PageDown);
            MNR_KEY_CASE(SDL_SCANCODE_PRINTSCREEN, PrintScreen);
            MNR_KEY_CASE(SDL_SCANCODE_SCROLLLOCK, ScrollLock);
            MNR_KEY_CASE(SDL_SCANCODE_PAUSE, Pause);

            MNR_KEY_CASE(SDL_SCANCODE_F1, F1);
            MNR_KEY_CASE(SDL_SCANCODE_F2, F2);
            MNR_KEY_CASE(SDL_SCANCODE_F3, F3);
            MNR_KEY_CASE(SDL_SCANCODE_F4, F4);
            MNR_KEY_CASE(SDL_SCANCODE_F5, F5);
            MNR_KEY_CASE(SDL_SCANCODE_F6, F6);
            MNR_KEY_CASE(SDL_SCANCODE_F7, F7);
            MNR_KEY_CASE(SDL_SCANCODE_F8, F8);
            MNR_KEY_CASE(SDL_SCANCODE_F9, F9);
            MNR_KEY_CASE(SDL_SCANCODE_F10, F10);
            MNR_KEY_CASE(SDL_SCANCODE_F11, F11);
            MNR_KEY_CASE(SDL_SCANCODE_F12, F12);
            MNR_KEY_CASE(SDL_SCANCODE_F13, F13);
            MNR_KEY_CASE(SDL_SCANCODE_F14, F14);
            MNR_KEY_CASE(SDL_SCANCODE_F15, F15);
            MNR_KEY_CASE(SDL_SCANCODE_F16, F16);
            MNR_KEY_CASE(SDL_SCANCODE_F17, F17);
            MNR_KEY_CASE(SDL_SCANCODE_F18, F18);
            MNR_KEY_CASE(SDL_SCANCODE_F19, F19);
            MNR_KEY_CASE(SDL_SCANCODE_F20, F20);
            MNR_KEY_CASE(SDL_SCANCODE_F21, F21);
            MNR_KEY_CASE(SDL_SCANCODE_F22, F22);
            MNR_KEY_CASE(SDL_SCANCODE_F23, F23);
            MNR_KEY_CASE(SDL_SCANCODE_F24, F24);

            MNR_KEY_CASE(SDL_SCANCODE_KP_0, Numpad0);
            MNR_KEY_CASE(SDL_SCANCODE_KP_1, Numpad1);
            MNR_KEY_CASE(SDL_SCANCODE_KP_2, Numpad2);
            MNR_KEY_CASE(SDL_SCANCODE_KP_3, Numpad3);
            MNR_KEY_CASE(SDL_SCANCODE_KP_4, Numpad4);
            MNR_KEY_CASE(SDL_SCANCODE_KP_5, Numpad5);
            MNR_KEY_CASE(SDL_SCANCODE_KP_6, Numpad6);
            MNR_KEY_CASE(SDL_SCANCODE_KP_7, Numpad7);
            MNR_KEY_CASE(SDL_SCANCODE_KP_8, Numpad8);
            MNR_KEY_CASE(SDL_SCANCODE_KP_9, Numpad9);
            MNR_KEY_CASE(SDL_SCANCODE_KP_DECIMAL, NumpadDecimal);
            MNR_KEY_CASE(SDL_SCANCODE_KP_DIVIDE, NumpadDivide);
            MNR_KEY_CASE(SDL_SCANCODE_KP_MULTIPLY, NumpadMultiply);
            MNR_KEY_CASE(SDL_SCANCODE_KP_MINUS, NumpadMinus);
            MNR_KEY_CASE(SDL_SCANCODE_KP_PLUS, NumpadPlus);
            MNR_KEY_CASE(SDL_SCANCODE_KP_ENTER, NumpadEnter);
            MNR_KEY_CASE(SDL_SCANCODE_KP_EQUALS, NumpadEquals);
            default:
                return Key::Unknown;
        }
#undef MNR_KEY_CASE
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
} // namespace Manro
