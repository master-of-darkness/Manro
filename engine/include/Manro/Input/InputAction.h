#pragma once

#include <Manro/Core/Types.h>

namespace Manro {
    enum class Key : u16 {
        Unknown = 0,
        W, A, S, D, Q, E,
        Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        Space, LeftShift, LeftCtrl,
        Escape, Tab, Enter,
        F1, F2, F3, F4, F11,
        Up, Down, Left, Right,
        Num0,
        B, C, F, G, H, I, J, K, L, M, N, O, P, R, T, U, V, X, Y, Z,
        Minus, Equals, LeftBracket, RightBracket,
        Backslash, Semicolon, Apostrophe, Grave, Comma, Period, Slash,
        Backspace, CapsLock,
        RightShift, RightCtrl, LeftAlt, RightAlt, LeftSuper, RightSuper, Menu,
        Insert, Delete, Home, End, PageUp, PageDown, PrintScreen, ScrollLock, Pause,
        F5, F6, F7, F8, F9, F10, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
        Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
        NumpadDecimal, NumpadDivide, NumpadMultiply, NumpadMinus, NumpadPlus, NumpadEnter, NumpadEquals,
        _Count
    };

    enum class MouseButton : u8 {
        Left = 0, Right, Middle, _Count
    };

    struct RawMouseDelta_t {
        f32 x{0.f}, y{0.f};
    };

    struct UserCmd_t {
        f32 MoveForward{0.f};
        f32 MoveRight{0.f};

        f32 ViewYaw{0.f};
        f32 ViewPitch{0.f};

        u32 Buttons{0};

        u32 TickCount{0};

        void SetButton(u32 bit, bool v) {
            if (v) Buttons |= (1u << bit);
            else Buttons &= ~(1u << bit);
        }

        bool GetButton(u32 bit) const { return (Buttons >> bit) & 1u; }
    };

    namespace ButtonBit {
        inline constexpr u32 Jump = 0;
        inline constexpr u32 Crouch = 1;
        inline constexpr u32 Attack = 2;
        inline constexpr u32 AltAttack = 3;
        inline constexpr u32 Use = 4;
        inline constexpr u32 GameBitStart = 16;
    }

    class IInputActionMap {
    public:
        virtual ~IInputActionMap() = default;

        virtual void BuildUserCmd(UserCmd_t &cmd) = 0;

        virtual void OnFocusLost() {
        }
    };
} // namespace Manro
