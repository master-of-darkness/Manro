#pragma once

#include <Manro/Core/Types.h>
#include <Manro/ECS/Entity.h>
#include <Manro/Input/InputAction.h>
#include <deque>
#include <unordered_map>
#include <functional>

struct _ENetHost_t;
struct _ENetPeer_t;

namespace Manro {
    class CRegistry;

    class CPhysicsWorld;

    struct InterpolationState_t {
        Vec3 previousPosition{0.f, 0.f, 0.f};
        Vec3 targetPosition{0.f, 0.f, 0.f};
        Vec3 targetVelocity{0.f, 0.f, 0.f};
        f32 previousTime{0.f};
        f32 targetTime{0.f};
        f32 lastUpdateTime{0.f};
    };

    struct RemoteEntitySnapshot_t {
        u32 entityId{0};
        Vec3 position{0.f, 0.f, 0.f};
        Vec3 velocity{0.f, 0.f, 0.f};
        Vec3 color{1.f, 1.f, 1.f};
        u8 entityType{0};
        bool isNew{false};
    };

    class CNetworkClient {
    public:
        CNetworkClient();

        ~CNetworkClient();

        void Connect(const std::string &address, u16 port);

        void Disconnect();

        void Tick(CRegistry &registry, CPhysicsWorld *physics, const UserCmd_t &cmd, f32 deltaTime);

        using OnEntitySpawnedFn = std::function<void(CRegistry &, const RemoteEntitySnapshot_t &)>;

        using OnEntityUpdatedFn = std::function<void(CRegistry &, const RemoteEntitySnapshot_t &)>;

        using OnEntityDespawnedFn = std::function<void(CRegistry &, u32 entityId)>;

        void SetOnEntitySpawned(OnEntitySpawnedFn fn) { m_OnEntitySpawned = std::move(fn); }

        void SetOnEntityUpdated(OnEntityUpdatedFn fn) { m_OnEntityUpdated = std::move(fn); }

        void SetOnEntityDespawned(OnEntityDespawnedFn fn) { m_OnEntityDespawned = std::move(fn); }

        void SetLocalPlayerEntityId(Entity id) { m_LocalPlayerEntityId = id; }

        f32 GetServerTime() const { return m_flServerTime; }

        f32 GetRoundTripTime() const { return m_flRoundTripTime; }

        Vec3 GetInterpolatedPosition(u32 entityId, f32 currentTime);

    private:
        void ProcessServerSnapshot(const void *data, size_t size,
                                   CRegistry &registry, CPhysicsWorld *physics);

        void ApplyInterpolation(CRegistry &registry, f32 currentTime);

        void ReconcileLocalPlayer(const Vec3 &serverPosition,
                                  CRegistry &registry, CPhysicsWorld *physics);

        bool m_bIsConnected{false};
        _ENetHost_t *m_ClientHost{nullptr};
        _ENetPeer_t *m_ServerPeer{nullptr};

        f32 m_flServerTime{0.f};
        f32 m_flRoundTripTime{0.1f};
        f32 m_flClientTime{0.f};

        std::unordered_map<u32, InterpolationState_t> m_InterpolationStates;

        Entity m_LocalPlayerEntityId{NULL_ENTITY};
        Entity m_ServerPlayerEntityId{NULL_ENTITY};
        Vec3 m_LastServerPosition{0.f, 0.f, 0.f};

        OnEntitySpawnedFn m_OnEntitySpawned;
        OnEntityUpdatedFn m_OnEntityUpdated;
        OnEntityDespawnedFn m_OnEntityDespawned;

        static constexpr f32 INTERPOLATION_DELAY = 0.1f;
        static constexpr f32 RECONCILE_THRESHOLD = 0.3f;
    };
} // namespace Manro