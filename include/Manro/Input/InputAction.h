#pragma once

#include <Manro/Core/Types.h>
#include <string>
#include <unordered_map>
#include <functional>

namespace Engine {
    enum class Key : u16 {
        Unknown = 0,
        W, A, S, D, Q, E,
        Space, LeftShift, LeftCtrl,
        Escape, Tab, Enter,
        F1, F2, F3, F4,
        Up, Down, Left, Right,
        _Count
    };

    enum class MouseButton : u8 { Left = 0, Right, Middle, _Count };

    struct RawMouseDelta {
        f32 x{0.f}, y{0.f};
    };

    struct UserCmd {
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

        virtual void BuildUserCmd(UserCmd &cmd) = 0;

        virtual void OnFocusLost() {
        }
    };
} // namespace Engine
