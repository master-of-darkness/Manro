#pragma once
#include <Manro/Core/Types.h>
#include <Manro/Core/Handles.h>
#include <cstdint>

namespace Manro {
    struct TransformComponent {
        Vec3 Position;
        Vec3 Rotation;
        Vec3 Scale;
    };

    enum class PhysicsBodyType : u8 {
        Static,
        Dynamic,
        Kinematic,
    };

    struct RigidBodyComponent {
        PhysicsBodyHandle BodyId{kInvalidBodyHandle};
        PhysicsBodyType Type{PhysicsBodyType::Static};

        Vec3 HalfExtents{0.5f, 0.5f, 0.5f};
        f32 CapsuleRadius{0.3f};
        f32 CapsuleHalfHeight{0.5f};


        bool IsValid() const { return BodyId != kInvalidBodyHandle; }
    };
} // namespace Manro
