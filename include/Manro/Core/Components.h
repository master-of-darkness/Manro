#pragma once
#include <Manro/Core/Types.h>
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
        u32 BodyId{0xFFFFFFFF};
        PhysicsBodyType Type{PhysicsBodyType::Static};

        Vec3 HalfExtents{0.5f, 0.5f, 0.5f};
        f32 CapsuleRadius{0.3f};
        f32 CapsuleHalfHeight{0.5f};

        Vec3 Velocity{0.0f, 0.0f, 0.0f};

        bool IsValid() const { return BodyId != 0xFFFFFFFF; }
    };
}
