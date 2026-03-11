#pragma once

#include <Manro/Core/Types.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <functional>

namespace Engine {
    class Registry;

    using PhysicsBodyHandle = u32;
    inline constexpr PhysicsBodyHandle kInvalidBodyHandle = 0xFFFFFFFF;

    using BodySyncCallback = std::function<void(u32 entity, const Vec3 &pos, const Vec3 &vel)>;

    class PhysicsWorld {
    public:
        PhysicsWorld();

        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld &) = delete;

        PhysicsWorld &operator=(const PhysicsWorld &) = delete;

        void Step(float deltaTime);

        PhysicsBodyHandle AddStaticBox(const Vec3 &position, const Vec3 &halfExtents);

        PhysicsBodyHandle AddDynamicBox(const Vec3 &position, const Vec3 &halfExtents, float mass = 1.f);

        PhysicsBodyHandle AddKinematicCapsule(const Vec3 &position, float radius, float halfHeight);

        PhysicsBodyHandle AddDynamicCone(const Vec3 &position, float radius, float height, float mass = 1.f);

        void RemoveBody(PhysicsBodyHandle handle);

        void SetBodyUserData(PhysicsBodyHandle handle, u32 entityId);

        u32 GetBodyUserData(PhysicsBodyHandle handle) const;

        Vec3 GetBodyPosition(PhysicsBodyHandle handle) const;

        void SetBodyPosition(PhysicsBodyHandle handle, const Vec3 &position);

        Vec3 GetBodyLinearVelocity(PhysicsBodyHandle handle) const;

        void SetLinearVelocity(PhysicsBodyHandle handle, const Vec3 &velocity);

        void ApplyLinearImpulse(PhysicsBodyHandle handle, const Vec3 &impulse);

        bool IsGrounded(PhysicsBodyHandle handle) const;

        void SetKinematicVelocity(PhysicsBodyHandle handle, const Vec3 &velocity);

        void WakeBodyAndNeighbours(PhysicsBodyHandle handle, float radius = 2.f);

        void ForEachDynamicBody(const BodySyncCallback &cb) const;

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
} // namespace Engine
