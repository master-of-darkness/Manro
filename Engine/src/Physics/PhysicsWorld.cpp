#include "PhysicsWorld.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/RegisterTypes.h>

#include <Core/Logger.h>
#include <thread>
#include <unordered_map>
#include <cmath>

#ifdef JPH_ENABLE_ASSERTS
static bool JoltAssertFailed(const char *expr, const char *msg,
                             const char *file, JPH::uint line) {
    LOG_ERROR("[Jolt Assert] {}:{} ({}) {}", file, line, expr, (msg ? msg : ""));
    return true;
}
#endif

namespace {
    namespace Layers {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::uint NUM_LAYERS = 2;
    }

    namespace BroadPhaseLayers {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr JPH::uint NUM_LAYERS = 2;
    }

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
    public:
        BPLayerInterfaceImpl() {
            m_Map[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            m_Map[Layers::MOVING] = BroadPhaseLayers::MOVING;
        }

        JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override { return m_Map[l]; }

    private:
        JPH::BroadPhaseLayer m_Map[Layers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer l1, JPH::BroadPhaseLayer l2) const override {
            switch (l1) {
                case Layers::NON_MOVING: return l2 == BroadPhaseLayers::MOVING;
                case Layers::MOVING: return true;
                default: return false;
            }
        }
    };

    class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer l1, JPH::ObjectLayer l2) const override {
            switch (l1) {
                case Layers::NON_MOVING: return l2 == Layers::MOVING;
                case Layers::MOVING: return true;
                default: return false;
            }
        }
    };

    static constexpr JPH::uint MAX_BODIES = 2048;
    static constexpr JPH::uint NUM_BODY_MUTEXES = 0;
    static constexpr JPH::uint MAX_BODY_PAIRS = 4096;
    static constexpr JPH::uint MAX_CONTACT_CONSTRAINTS = 2048;

    inline Engine::u32 toHandle(JPH::BodyID id) { return id.GetIndexAndSequenceNumber(); }
    inline JPH::BodyID fromHandle(Engine::u32 h) { return JPH::BodyID(h); }
} // anonymous namespace

namespace Engine {
    struct PhysicsWorld::Impl {
        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
        std::unique_ptr<JPH::PhysicsSystem> physicsSystem;

        BPLayerInterfaceImpl bpLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl objVsBroadPhase;
        ObjectLayerPairFilterImpl objLayerFilter;

        std::unordered_map<u32, u32> bodyToEntity;
    };

    PhysicsWorld::PhysicsWorld() {
        JPH::RegisterDefaultAllocator();
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = JoltAssertFailed;
#endif
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        m_Impl = std::make_unique<Impl>();
        m_Impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
        m_Impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
            static_cast<int>(std::thread::hardware_concurrency()) - 1);

        m_Impl->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
        m_Impl->physicsSystem->Init(
            MAX_BODIES, NUM_BODY_MUTEXES, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS,
            m_Impl->bpLayerInterface, m_Impl->objVsBroadPhase, m_Impl->objLayerFilter);
        m_Impl->physicsSystem->SetGravity(JPH::Vec3(0.f, -9.81f, 0.f));

        LOG_INFO("[PhysicsWorld] Jolt Physics initialized.");
    }

    PhysicsWorld::~PhysicsWorld() {
        m_Impl.reset();
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        LOG_INFO("[PhysicsWorld] Jolt Physics shut down.");
    }


    void PhysicsWorld::Step(float deltaTime) {
        m_Accumulator += (deltaTime > 0.25f) ? 0.25f : deltaTime;

        while (m_Accumulator >= FIXED_STEP) {
            auto &bi = m_Impl->physicsSystem->GetBodyInterface();
            for (auto &km: m_PendingKinematicMoves) {
                JPH::BodyID bid = fromHandle(km.rawId);
                if (bid.IsInvalid()) continue;
                JPH::RVec3 cur = bi.GetPosition(bid);
                JPH::RVec3 target(
                    cur.GetX() + km.velocity.x * FIXED_STEP,
                    cur.GetY() + km.velocity.y * FIXED_STEP,
                    cur.GetZ() + km.velocity.z * FIXED_STEP);
                bi.MoveKinematic(bid, target, JPH::Quat::sIdentity(), FIXED_STEP);
            }
            m_Impl->physicsSystem->Update(FIXED_STEP, 1,
                                          m_Impl->tempAllocator.get(), m_Impl->jobSystem.get());
            m_Accumulator -= FIXED_STEP;
        }
        m_PendingKinematicMoves.clear();
    }

    void PhysicsWorld::SetBodyUserData(PhysicsBodyHandle handle, u32 entityId) {
        m_Impl->bodyToEntity[handle] = entityId;
    }

    u32 PhysicsWorld::GetBodyUserData(PhysicsBodyHandle handle) const {
        auto it = m_Impl->bodyToEntity.find(handle);
        return (it != m_Impl->bodyToEntity.end()) ? it->second : 0xFFFFFFFF;
    }

    void PhysicsWorld::ForEachDynamicBody(const BodySyncCallback &cb) const {
        if (!cb) return;

        auto &bi = m_Impl->physicsSystem->GetBodyInterface();

        for (auto &[handle, entityId]: m_Impl->bodyToEntity) {
            JPH::BodyID id = fromHandle(handle);
            if (id.IsInvalid()) continue;

            JPH::EMotionType motion = bi.GetMotionType(id);
            if (motion == JPH::EMotionType::Static) continue;

            JPH::RVec3 jpos = bi.GetPosition(id);
            JPH::Vec3 jvel = bi.GetLinearVelocity(id);

            Vec3 pos(static_cast<f32>(jpos.GetX()),
                     static_cast<f32>(jpos.GetY()),
                     static_cast<f32>(jpos.GetZ()));
            Vec3 vel(jvel.GetX(), jvel.GetY(), jvel.GetZ());

            cb(entityId, pos, vel);
        }
    }

    PhysicsBodyHandle PhysicsWorld::AddStaticBox(const Vec3 &position, const Vec3 &halfExtents) {
        auto &bi = m_Impl->physicsSystem->GetBodyInterface();
        JPH::BoxShapeSettings ss(JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
        ss.mConvexRadius = 0.01f;
        auto sr = ss.Create();
        if (sr.HasError()) {
            LOG_ERROR("[PhysicsWorld] {}", sr.GetError());
            return kInvalidBodyHandle;
        }
        JPH::BodyCreationSettings bcs(sr.Get(),
                                      JPH::RVec3(position.x, position.y, position.z),
                                      JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
        JPH::Body *body = bi.CreateBody(bcs);
        if (!body) return kInvalidBodyHandle;
        bi.AddBody(body->GetID(), JPH::EActivation::DontActivate);
        return toHandle(body->GetID());
    }

    PhysicsBodyHandle PhysicsWorld::AddDynamicBox(const Vec3 &position, const Vec3 &halfExtents, float mass) {
        auto &bi = m_Impl->physicsSystem->GetBodyInterface();
        JPH::BoxShapeSettings ss(JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
        ss.mConvexRadius = 0.02f;
        auto sr = ss.Create();
        if (sr.HasError()) {
            LOG_ERROR("[PhysicsWorld] {}", sr.GetError());
            return kInvalidBodyHandle;
        }
        JPH::BodyCreationSettings bcs(sr.Get(),
                                      JPH::RVec3(position.x, position.y, position.z),
                                      JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::MOVING);
        bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = mass;
        bcs.mAllowSleeping = false;
        JPH::Body *body = bi.CreateBody(bcs);
        if (!body) return kInvalidBodyHandle;
        bi.AddBody(body->GetID(), JPH::EActivation::Activate);
        return toHandle(body->GetID());
    }

    PhysicsBodyHandle PhysicsWorld::AddKinematicCapsule(const Vec3 &position, float radius, float halfHeight) {
        auto &bi = m_Impl->physicsSystem->GetBodyInterface();
        JPH::CapsuleShapeSettings cs(halfHeight, radius);
        auto sr = cs.Create();
        if (sr.HasError()) {
            LOG_ERROR("[PhysicsWorld] {}", sr.GetError());
            return kInvalidBodyHandle;
        }
        float centerOffset = halfHeight + radius;
        JPH::RotatedTranslatedShapeSettings rts(JPH::Vec3(0.f, centerOffset, 0.f), JPH::Quat::sIdentity(), sr.Get());
        auto rsr = rts.Create();
        if (rsr.HasError()) {
            LOG_ERROR("[PhysicsWorld] {}", rsr.GetError());
            return kInvalidBodyHandle;
        }
        JPH::BodyCreationSettings bcs(rsr.Get(),
                                      JPH::RVec3(position.x, position.y, position.z),
                                      JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, Layers::MOVING);
        bcs.mAllowSleeping = false;
        JPH::Body *body = bi.CreateBody(bcs);
        if (!body) return kInvalidBodyHandle;
        bi.AddBody(body->GetID(), JPH::EActivation::Activate);
        return toHandle(body->GetID());
    }

    PhysicsBodyHandle PhysicsWorld::AddDynamicCone(const Vec3 &position, float radius, float height, float mass) {
        auto &bi = m_Impl->physicsSystem->GetBodyInterface();
        std::vector<JPH::Vec3> points;
        points.push_back(JPH::Vec3(0, height * 0.5f, 0));
        for (int i = 0; i < 12; ++i) {
            float angle = static_cast<float>(i) / 12.f * 2.f * 3.14159265f;
            points.push_back(JPH::Vec3(radius * cosf(angle), -height * 0.5f, radius * sinf(angle)));
        }
        JPH::ConvexHullShapeSettings cs(points.data(), static_cast<int>(points.size()));
        auto sr = cs.Create();
        if (sr.HasError()) {
            LOG_ERROR("[PhysicsWorld] {}", sr.GetError());
            return kInvalidBodyHandle;
        }
        JPH::BodyCreationSettings bcs(sr.Get(),
                                      JPH::RVec3(position.x, position.y + height * 0.5f, position.z),
                                      JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::MOVING);
        bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = mass;
        bcs.mAllowSleeping = false;
        bcs.mMaxLinearVelocity = 100.f;
        bcs.mMaxAngularVelocity = 0.f;
        bcs.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY |
                           JPH::EAllowedDOFs::TranslationZ;
        JPH::Body *body = bi.CreateBody(bcs);
        if (!body) return kInvalidBodyHandle;
        bi.AddBody(body->GetID(), JPH::EActivation::Activate);
        return toHandle(body->GetID());
    }

    void PhysicsWorld::RemoveBody(PhysicsBodyHandle handle) {
        JPH::BodyID id = fromHandle(handle);
        if (id.IsInvalid()) return;
        auto &bi = m_Impl->physicsSystem->GetBodyInterface();
        bi.RemoveBody(id);
        bi.DestroyBody(id);
        m_Impl->bodyToEntity.erase(handle);
    }

    bool PhysicsWorld::IsGrounded(PhysicsBodyHandle handle) const {
        JPH::BodyID id = fromHandle(handle);
        if (id.IsInvalid()) return false;
        auto &bi = m_Impl->physicsSystem->GetBodyInterface();
        JPH::RVec3 pos = bi.GetPosition(id);
        JPH::BodyLockRead lock(m_Impl->physicsSystem->GetBodyLockInterface(), id);
        if (!lock.Succeeded()) return false;
        float extent = lock.GetBody().GetShape()->GetLocalBounds().GetExtent().GetY();
        JPH::RVec3 origin = pos + JPH::Vec3(0, -extent + 0.1f, 0);
        JPH::RRayCast ray{origin, JPH::Vec3(0, -0.2f, 0)};
        JPH::RayCastResult hit;
        JPH::IgnoreSingleBodyFilter bodyFilter(id);
        return m_Impl->physicsSystem->GetNarrowPhaseQuery().CastRay(
            ray, hit,
            JPH::SpecifiedBroadPhaseLayerFilter(BroadPhaseLayers::NON_MOVING),
            JPH::SpecifiedObjectLayerFilter(Layers::NON_MOVING),
            bodyFilter);
    }

    void PhysicsWorld::ApplyLinearImpulse(PhysicsBodyHandle handle, const Vec3 &impulse) {
        JPH::BodyID id = fromHandle(handle);
        if (id.IsInvalid()) return;
        m_Impl->physicsSystem->GetBodyInterface().AddImpulse(id, JPH::Vec3(impulse.x, impulse.y, impulse.z));
    }

    Vec3 PhysicsWorld::GetBodyPosition(PhysicsBodyHandle handle) const {
        JPH::BodyID id = fromHandle(handle);
        if (id.IsInvalid()) return Vec3(0.f);
        JPH::RVec3 pos = m_Impl->physicsSystem->GetBodyInterface().GetPosition(id);
        return Vec3(static_cast<f32>(pos.GetX()), static_cast<f32>(pos.GetY()), static_cast<f32>(pos.GetZ()));
    }

    void PhysicsWorld::SetBodyPosition(PhysicsBodyHandle handle, const Vec3 &position) {
        JPH::BodyID id = fromHandle(handle);
        if (id.IsInvalid()) return;
        m_Impl->physicsSystem->GetBodyInterface().SetPosition(
            id, JPH::RVec3(position.x, position.y, position.z), JPH::EActivation::Activate);
    }

    Vec3 PhysicsWorld::GetBodyLinearVelocity(PhysicsBodyHandle handle) const {
        JPH::BodyID id = fromHandle(handle);
        if (id.IsInvalid()) return Vec3(0.f);
        JPH::Vec3 vel = m_Impl->physicsSystem->GetBodyInterface().GetLinearVelocity(id);
        return Vec3(vel.GetX(), vel.GetY(), vel.GetZ());
    }

    void PhysicsWorld::SetLinearVelocity(PhysicsBodyHandle handle, const Vec3 &velocity) {
        JPH::BodyID id = fromHandle(handle);
        if (id.IsInvalid()) return;
        m_Impl->physicsSystem->GetBodyInterface().SetLinearVelocity(
            id, JPH::Vec3(velocity.x, velocity.y, velocity.z));
    }

    void PhysicsWorld::SetKinematicVelocity(PhysicsBodyHandle handle, const Vec3 &velocity) {
        m_PendingKinematicMoves.push_back({handle, velocity});
    }

    void PhysicsWorld::WakeBodyAndNeighbours(PhysicsBodyHandle handle, float radius) {
        JPH::BodyID id = fromHandle(handle);
        if (id.IsInvalid()) return;
        auto &bi = m_Impl->physicsSystem->GetBodyInterface();
        auto &bpq = m_Impl->physicsSystem->GetBroadPhaseQuery();
        JPH::RVec3 pos = bi.GetCenterOfMassPosition(id);
        float px = static_cast<f32>(pos.GetX()), py = static_cast<f32>(pos.GetY()), pz = static_cast<f32>(pos.GetZ());
        JPH::AABox box(JPH::Vec3(px - radius, py - radius, pz - radius),
                       JPH::Vec3(px + radius, py + radius, pz + radius));
        class WC : public JPH::CollideShapeBodyCollector {
        public:
            std::vector<JPH::BodyID> hits;
            void AddHit(const JPH::BodyID &b) override { hits.push_back(b); }
        } collector;
        bpq.CollideAABox(box, collector, JPH::BroadPhaseLayerFilter{}, JPH::ObjectLayerFilter{});
        for (auto &hit: collector.hits) {
            if (hit == id) continue;
            if (!bi.IsActive(hit)) bi.ActivateBody(hit);
        }
    }
} // namespace Engine
