#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Handles.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <functional>

namespace Manro {
    class Registry;

    using BodySyncCallback = std::function<void(u32 entity, const Vec3 & pos, const Vec3 & vel)>;

    class PhysicsWorld {
    public:
        struct StaticBodyDesc {
            float friction;
            float restitution;
            float convexRadius;

            StaticBodyDesc(float friction = 0.2f, float restitution = 0.f, float convexRadius = 0.01f)
                : friction(friction), restitution(restitution), convexRadius(convexRadius) {
            }
        };

        struct DynamicBodyDesc {
            float mass;
            float friction;
            float restitution;
            float convexRadius;
            bool allowSleeping;
            bool lockRotation;
            float maxLinearVelocity;
            float maxAngularVelocity;

            DynamicBodyDesc(float mass = 1.f,
                            float friction = 0.2f,
                            float restitution = 0.f,
                            float convexRadius = 0.02f,
                            bool allowSleeping = false,
                            bool lockRotation = false,
                            float maxLinearVelocity = 50000.f,
                            float maxAngularVelocity = 100.f)
                : mass(mass),
                  friction(friction),
                  restitution(restitution),
                  convexRadius(convexRadius),
                  allowSleeping(allowSleeping),
                  lockRotation(lockRotation),
                  maxLinearVelocity(maxLinearVelocity),
                  maxAngularVelocity(maxAngularVelocity) {
            }
        };

        struct RaycastHit {
            PhysicsBodyHandle body{kInvalidBodyHandle};
            Vec3 position{0.f};
            float fraction{0.f};
            Vec3 normal{0.f};
        };

        PhysicsWorld();

        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld &) = delete;

        PhysicsWorld &operator=(const PhysicsWorld &) = delete;

        void Step(float deltaTime);

        PhysicsBodyHandle AddStaticBox(const Vec3 &position, const Vec3 &halfExtents,
                                       const StaticBodyDesc &desc = {});

        PhysicsBodyHandle AddDynamicBox(const Vec3 &position, const Vec3 &halfExtents,
                                        const DynamicBodyDesc &desc = {});

        PhysicsBodyHandle AddDynamicCapsule(const Vec3 &position, float radius, float halfHeight,
                                            const DynamicBodyDesc &desc = {});

        PhysicsBodyHandle AddStaticMesh(const std::vector<Vec3> &vertices, const std::vector<u32> &indices,
                                        const Mat4 &transform = Mat4(1.f));

        PhysicsBodyHandle AddDynamicCone(const Vec3 &position, float radius, float height,
                                         const DynamicBodyDesc &desc = {});

        void RemoveBody(PhysicsBodyHandle handle);

        void SetBodyUserData(PhysicsBodyHandle handle, u32 entityId);

        u32 GetBodyUserData(PhysicsBodyHandle handle) const;

        Vec3 GetBodyPosition(PhysicsBodyHandle handle) const;

        void SetBodyPosition(PhysicsBodyHandle handle, const Vec3 &position);

        Vec3 GetBodyLinearVelocity(PhysicsBodyHandle handle) const;

        void SetLinearVelocity(PhysicsBodyHandle handle, const Vec3 &velocity);

        void SetBodyMotionType(PhysicsBodyHandle handle, bool kinematic);

        void ApplyLinearImpulse(PhysicsBodyHandle handle, const Vec3 &impulse);

        bool IsGrounded(PhysicsBodyHandle handle) const;

        bool RaycastClosest(const Vec3 &origin, const Vec3 &direction, float distance,
                            RaycastHit &outHit, PhysicsBodyHandle ignore = kInvalidBodyHandle) const;

        void SetKinematicVelocity(PhysicsBodyHandle handle, const Vec3 &velocity);

        void WakeBodyAndNeighbours(PhysicsBodyHandle handle, float radius = 2.f);

        void ForEachDynamicBody(const BodySyncCallback &cb) const;

        void DrawPhysics(class Renderer &renderer) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;

        float m_Accumulator{0.f};

        static constexpr float FIXED_STEP = 1.f / 60.f;

        struct KinematicMove {
            u32 rawId;
            Vec3 velocity;
        };

        std::vector<KinematicMove> m_PendingKinematicMoves;
    };
} // namespace Manro