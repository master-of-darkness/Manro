#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Handles.h>

namespace Manro {
    struct TransformComponent_t {
        Vec3 Position;
        Vec3 Rotation;
        Vec3 Scale;
    };

    struct RigidBodyComponent_t {
        PhysicsBodyHandle BodyId{kInvalidBodyHandle};
        PhysicsBodyType Type{PhysicsBodyType::Static};

        Vec3 HalfExtents{0.5f, 0.5f, 0.5f};
        f32 CapsuleRadius{0.3f};
        f32 CapsuleHalfHeight{0.5f};


        bool IsValid() const { return BodyId != kInvalidBodyHandle; }
    };
} // namespace Manro
