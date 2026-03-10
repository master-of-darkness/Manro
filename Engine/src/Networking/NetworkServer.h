#pragma once

#include <Core/Types.h>
#include <ECS/Entity.h>
#include <Physics/IMovementController.h>
#include <Input/InputAction.h>
#include <enet/enet.h>
#include <unordered_map>
#include <functional>
#include "NetworkMessage_generated.h"

namespace Engine {
    class Registry;
    class PhysicsWorld;

    struct ServerPlayerState {
        u32 clientId{0};
        Entity entityId{NULL_ENTITY};
        u32 bodyHandle{0xFFFFFFFF}; // PhysicsBodyHandle
    };

    struct Connection {
        u32 Id;
        ENetPeer *Peer;
        bool IsActive;
    };

    struct ClientInputData {
        f32 moveForward{0.f};
        f32 moveRight{0.f};
        f32 viewYaw{0.f};
        f32 viewPitch{0.f};
        u32 buttons{0};
        bool jumpWasHeld{false};
    };

    class NetworkServer {
    public:
        explicit NetworkServer(u16 port);

        ~NetworkServer();

        NetworkServer(const NetworkServer &) = delete;

        NetworkServer &operator=(const NetworkServer &) = delete;

        void Tick(Registry &registry,
                  PhysicsWorld &physics,
                  IMovementController &movementController,
                  float deltaTime);

        void BroadcastSnapshot(Registry &registry, PhysicsWorld &physics);

        f32 GetServerTime() const { return m_ServerTime; }

        using SpawnPlayerFn = std::function<Entity(u32 clientId)>;
        using DespawnPlayerFn = std::function<void(u32 clientId, Entity entity)>;

        void SetSpawnPlayerCallback(SpawnPlayerFn fn) { m_SpawnPlayer = std::move(fn); }
        void SetDespawnPlayerCallback(DespawnPlayerFn fn) { m_DespawnPlayer = std::move(fn); }

        const std::unordered_map<u32, ServerPlayerState> &GetPlayers() const { return m_Players; }

    private:
        void HandleNewConnection(ENetPeer *peer);

        void HandleDisconnection(ENetPeer *peer, Registry &registry);

        void ValidateAndStoreInput(u32 clientId, const Networking::ClientInput *inp);

        u16 m_Port{0};
        ENetHost *m_ServerHost{nullptr};

        std::unordered_map<u32, Connection> m_Connections;
        std::unordered_map<u32, ClientInputData> m_ClientInputs;
        std::unordered_map<u32, ServerPlayerState> m_Players;
        u32 m_NextConnectionId{1};

        SpawnPlayerFn m_SpawnPlayer;
        DespawnPlayerFn m_DespawnPlayer;

        f32 m_ServerTime{0.f};
        f32 m_LastSnapshotTime{0.f};
        static constexpr f32 SNAPSHOT_RATE = 0.05f;
    };
} // namespace Engine