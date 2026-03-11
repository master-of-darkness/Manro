#pragma once
#include <Manro/Core/Types.h>
#include <cstdint>

namespace Engine {
    struct TransformComponent {
        Vec3 Position;
        Vec3 Rotation;
        Vec3 Scale;
    };

    struct CameraComponent {
        f32 Fov{45.0f};
        f32 NearClip{0.1f};
        f32 FarClip{100.0f};
        f32 Yaw{-90.0f};
        f32 Pitch{-15.0f};
    };

    struct PlayerControllerComponent {
        f32 Speed{5.0f};
        f32 TurnSpeed{2.0f};
    };

    struct MeshRenderComponent {
        u32 MeshId{0};
    };

    struct ColorComponent {
        Vec3 Color{1.0f, 1.0f, 1.0f};
    };

    struct StaticMeshTag {
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

    struct JumpStateComponent {
        bool WasJumpPressed{false};
    };
}
