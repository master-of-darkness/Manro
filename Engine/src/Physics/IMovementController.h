#pragma once

#include <Core/Types.h>
#include <Input/InputAction.h>

namespace Engine {
    class PhysicsWorld;

    struct MovementContext {
        PhysicsWorld *Physics{nullptr};
        u32 BodyHandle{0xFFFFFFFF}; // PhysicsBodyHandle
        const UserCmd *Cmd{nullptr};
        f32 DeltaTime{0.f};
    };

    class IMovementController {
    public:
        virtual ~IMovementController() = default;

        virtual void ProcessMovement(const MovementContext &ctx) = 0;
    };
} // namespace Engine