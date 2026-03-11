#pragma once

#include <Manro/Core/Types.h>
#include <Manro/ECS/Entity.h>
#include <Manro/Input/InputAction.h>
#include <enet/enet.h>
#include <deque>
#include <unordered_map>
#include <functional>

namespace Engine {
    class Registry;
    class PhysicsWorld;

    struct InterpolationState {
        Vec3 previousPosition{0.f, 0.f, 0.f};
        Vec3 targetPosition{0.f, 0.f, 0.f};
        Vec3 targetVelocity{0.f, 0.f, 0.f};
        f32 previousTime{0.f};
        f32 targetTime{0.f};
        f32 lastUpdateTime{0.f};
    };

    struct RemoteEntitySnapshot {
        u32 entityId{0};
        Vec3 position{0.f, 0.f, 0.f};
        Vec3 velocity{0.f, 0.f, 0.f};
        Vec3 color{1.f, 1.f, 1.f};
        u8 entityType{0};
        bool isNew{false};
    };

    class NetworkClient {
    public:
        NetworkClient();

        ~NetworkClient();

        void Connect(const std::string &address, u16 port);

        void Disconnect();

        void Tick(Registry &registry, PhysicsWorld *physics, const UserCmd &cmd, f32 deltaTime);

        using OnEntitySpawnedFn = std::function<void(Registry &, const RemoteEntitySnapshot &)>;

        using OnEntityUpdatedFn = std::function<void(Registry &, const RemoteEntitySnapshot &)>;

        using OnEntityDespawnedFn = std::function<void(Registry &, u32 entityId)>;

        void SetOnEntitySpawned(OnEntitySpawnedFn fn) { m_OnEntitySpawned = std::move(fn); }
        void SetOnEntityUpdated(OnEntityUpdatedFn fn) { m_OnEntityUpdated = std::move(fn); }
        void SetOnEntityDespawned(OnEntityDespawnedFn fn) { m_OnEntityDespawned = std::move(fn); }

        void SetLocalPlayerEntityId(Entity id) { m_LocalPlayerEntityId = id; }

        f32 GetServerTime() const { return m_ServerTime; }
        f32 GetRoundTripTime() const { return m_RoundTripTime; }

        Vec3 GetInterpolatedPosition(u32 entityId, f32 currentTime);

    private:
        void ProcessServerSnapshot(const void *data, size_t size,
                                   Registry &registry, PhysicsWorld *physics);

        void ApplyInterpolation(Registry &registry, f32 currentTime);

        void ReconcileLocalPlayer(const Vec3 &serverPosition,
                                  Registry &registry, PhysicsWorld *physics);

        bool m_IsConnected{false};
        ENetHost *m_ClientHost{nullptr};
        ENetPeer *m_ServerPeer{nullptr};

        f32 m_ServerTime{0.f};
        f32 m_RoundTripTime{0.1f};
        f32 m_ClientTime{0.f};

        std::unordered_map<u32, InterpolationState> m_InterpolationStates;

        Entity m_LocalPlayerEntityId{NULL_ENTITY};
        Entity m_ServerPlayerEntityId{NULL_ENTITY};
        Vec3 m_LastServerPosition{0.f, 0.f, 0.f};

        OnEntitySpawnedFn m_OnEntitySpawned;
        OnEntityUpdatedFn m_OnEntityUpdated;
        OnEntityDespawnedFn m_OnEntityDespawned;

        static constexpr f32 INTERPOLATION_DELAY = 0.1f;
        static constexpr f32 RECONCILE_THRESHOLD = 0.3f;
    };
} // namespace Engine
