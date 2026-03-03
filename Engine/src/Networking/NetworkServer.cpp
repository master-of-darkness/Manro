#include "NetworkServer.h"
#include <ECS/Registry.h>
#include <Core/Components.h>
#include <Core/Logger.h>
#include <Physics/PhysicsWorld.h>
#include <Physics/IMovementController.h>
#include "NetworkMessage_generated.h"
#include <cmath>
#include <algorithm>

namespace Engine {
    NetworkServer::NetworkServer() = default;

    NetworkServer::~NetworkServer() { Shutdown(); }

    void NetworkServer::Initialize(u16 port) {
        if (enet_initialize() != 0) {
            LOG_ERROR("[Server] ENet init failed.");
            return;
        }
        m_Port = port;
        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = m_Port;
        m_ServerHost = enet_host_create(&address, 32, 2, 0, 0);
        if (!m_ServerHost) {
            LOG_ERROR("[Server] Failed to create ENet host.");
            return;
        }
        m_IsListening = true;
        m_ServerTime = 0.f;
        m_LastSnapshotTime = 0.f;
        LOG_INFO("[Server] Listening on UDP: {}", m_Port);
    }

    void NetworkServer::Shutdown() {
        if (!m_IsListening) return;
        if (m_ServerHost) {
            enet_host_destroy(m_ServerHost);
            m_ServerHost = nullptr;
        }
        enet_deinitialize();
        m_IsListening = false;
        LOG_INFO("[Server] Stopped.");
    }

    void NetworkServer::Tick(Registry &registry,
                             PhysicsWorld &physics,
                             IMovementController &movementController,
                             float deltaTime) {
        if (!m_ServerHost) return;
        m_ServerTime += deltaTime;

        ENetEvent event;
        while (enet_host_service(m_ServerHost, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    HandleNewConnection(event.peer);
                    break;
                case ENET_EVENT_TYPE_RECEIVE: {
                    auto msg = Networking::GetMessage(event.packet->data);
                    if (msg->payload_type() == Networking::Payload_ClientInput) {
                        u32 clientId = static_cast<u32>(reinterpret_cast<uintptr_t>(event.peer->data));
                        ValidateAndStoreInput(clientId, msg->payload_as_ClientInput());
                    }
                    enet_packet_destroy(event.packet);
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                    HandleDisconnection(event.peer, registry);
                    event.peer->data = nullptr;
                    break;
                default: break;
            }
        }

        for (auto &[clientId, conn]: m_Connections) {
            if (m_Players.count(clientId)) continue;
            if (!m_SpawnPlayer) continue;

            Entity e = m_SpawnPlayer(clientId);
            ServerPlayerState state;
            state.clientId = clientId;
            state.entityId = e;
            if (registry.HasComponent<RigidBodyComponent>(e))
                state.bodyHandle = registry.GetComponent<RigidBodyComponent>(e).BodyId;

            m_Players[clientId] = state;
            LOG_INFO("[Server] Spawned entity {} for client {}", e, clientId);

            Vec3 p{0.f, 0.f, 0.f};
            if (registry.HasComponent<TransformComponent>(e))
                p = registry.GetComponent<TransformComponent>(e).Position;

            flatbuffers::FlatBufferBuilder builder(128);
            Networking::Vec3 netPos(p.x, p.y, p.z);
            auto assign = Networking::CreateAssignPlayer(builder, e, &netPos);
            auto assignMsg = Networking::CreateMessage(builder, Networking::Payload_AssignPlayer, assign.Union());
            builder.Finish(assignMsg);
            ENetPacket *pkt = enet_packet_create(builder.GetBufferPointer(), builder.GetSize(),
                                                 ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(conn.Peer, 0, pkt);
        }

        for (auto &[clientId, input]: m_ClientInputs) {
            auto playerIt = m_Players.find(clientId);
            if (playerIt == m_Players.end()) continue;

            const u32 bodyHandle = playerIt->second.bodyHandle;
            if (bodyHandle == kInvalidBodyHandle) continue;

            UserCmd cmd{};
            cmd.MoveForward = input.moveForward;
            cmd.MoveRight = input.moveRight;
            cmd.ViewYaw = input.viewYaw;
            cmd.ViewPitch = input.viewPitch;
            cmd.Buttons = input.buttons;

            MovementContext ctx{};
            ctx.Physics = &physics;
            ctx.BodyHandle = bodyHandle;
            ctx.Cmd = &cmd;
            ctx.DeltaTime = deltaTime;

            movementController.ProcessMovement(ctx);
        }

        physics.Step(deltaTime);

        physics.ForEachDynamicBody([&](u32 entityId, const Vec3 &pos, const Vec3 &vel) {
            Entity e = static_cast<Entity>(entityId);
            if (registry.HasComponent<TransformComponent>(e))
                registry.GetComponent<TransformComponent>(e).Position = pos;
            if (registry.HasComponent<RigidBodyComponent>(e))
                registry.GetComponent<RigidBodyComponent>(e).Velocity = vel;
        });

        if (m_ServerTime - m_LastSnapshotTime >= SNAPSHOT_RATE) {
            BroadcastSnapshot(registry, physics);
            m_LastSnapshotTime = m_ServerTime;
        }
    }

    void NetworkServer::BroadcastSnapshot(Registry &registry, PhysicsWorld &physics) {
        if (!m_ServerHost) return;

        flatbuffers::FlatBufferBuilder builder(1024);
        std::vector<flatbuffers::Offset<Networking::EntitySnapshot> > entitySnapshots;

        std::unordered_map<Entity, uint8_t> entityTypes;
        for (auto &[cid, player]: m_Players)
            if (player.entityId != NULL_ENTITY)
                entityTypes[player.entityId] = 1; // 1 = player

        auto transforms = registry.GetComponentArray<TransformComponent>();
        if (transforms) {
            for (size_t i = 0; i < transforms->GetSize(); ++i) {
                const auto &transform = transforms->GetDenseArray()[i];
                const Entity entityId = transforms->GetDenseToEntityMap()[i];

                uint8_t eType = 2; // default = prop
                auto it = entityTypes.find(entityId);
                if (it != entityTypes.end()) eType = it->second;

                Networking::Vec3 pos(transform.Position.x, transform.Position.y, transform.Position.z);

                Networking::Vec3 color(1.f, 1.f, 1.f);
                if (registry.HasComponent<ColorComponent>(entityId)) {
                    auto &col = registry.GetComponent<ColorComponent>(entityId);
                    color = Networking::Vec3(col.Color.x, col.Color.y, col.Color.z);
                }

                Networking::Vec3 velocity(0.f, 0.f, 0.f);
                if (registry.HasComponent<RigidBodyComponent>(entityId)) {
                    auto &rb = registry.GetComponent<RigidBodyComponent>(entityId);
                    velocity = Networking::Vec3(rb.Velocity.x, rb.Velocity.y, rb.Velocity.z);
                }

                entitySnapshots.push_back(
                    Networking::CreateEntitySnapshot(builder, entityId, &pos, eType, &color, &velocity));
            }
        }

        auto entVec = builder.CreateVector(entitySnapshots);
        auto snap = Networking::CreateSnapshotPacket(builder, entVec);
        auto msg = Networking::CreateMessage(builder, Networking::Payload_SnapshotPacket, snap.Union());
        builder.Finish(msg);

        ENetPacket *pkt = enet_packet_create(builder.GetBufferPointer(), builder.GetSize(),
                                             ENET_PACKET_FLAG_UNSEQUENCED);
        enet_host_broadcast(m_ServerHost, 0, pkt);
    }

    void NetworkServer::ValidateAndStoreInput(u32 clientId, const Networking::ClientInput *inp) {
        if (!inp) return;
        auto &stored = m_ClientInputs[clientId];
        const bool prevJump = stored.jumpWasHeld;

        stored.moveForward = std::clamp(inp->move_forward(), -1.f, 1.f);
        stored.moveRight = std::clamp(inp->move_right(), -1.f, 1.f);
        stored.viewYaw = std::clamp(inp->view_yaw(), -360.f, 360.f);
        stored.viewPitch = std::clamp(inp->view_pitch(), -89.f, 89.f);
        stored.buttons = inp->buttons();
        stored.jumpWasHeld = prevJump; // edge detection preserved across frames
    }

    void NetworkServer::HandleNewConnection(ENetPeer *peer) {
        LOG_INFO("[Server] Client connected {}:{}", peer->address.host, peer->address.port);
        peer->data = reinterpret_cast<void *>(static_cast<uintptr_t>(m_NextConnectionId));
        m_Connections[m_NextConnectionId] = Connection{m_NextConnectionId, peer, true};
        m_NextConnectionId++;
    }

    void NetworkServer::HandleDisconnection(ENetPeer *peer, Registry &registry) {
        u32 clientId = static_cast<u32>(reinterpret_cast<uintptr_t>(peer->data));
        LOG_INFO("[Server] Client {} disconnected.", clientId);
        auto playerIt = m_Players.find(clientId);
        if (playerIt != m_Players.end()) {
            if (m_DespawnPlayer) m_DespawnPlayer(clientId, playerIt->second.entityId);
            m_Players.erase(playerIt);
        }
        m_Connections.erase(clientId);
        m_ClientInputs.erase(clientId);
    }
} // namespace Engine
