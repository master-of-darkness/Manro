#include <Manro/Networking/NetworkClient.h>
#include <Manro/ECS/Registry.h>
#include <Manro/Core/Components.h>
#include <Manro/Core/Logger.h>
#include <Manro/Physics/PhysicsWorld.h>
#include "NetworkMessage_generated.h"
#include <algorithm>

namespace Engine {
    NetworkClient::NetworkClient() = default;

    NetworkClient::~NetworkClient() { Disconnect(); }

    void NetworkClient::Connect(const std::string &address, u16 port) {
        if (enet_initialize() != 0) {
            LOG_ERROR("[Client] ENet init failed.");
            return;
        }
        m_ClientHost = enet_host_create(nullptr, 1, 2, 0, 0);
        if (!m_ClientHost) {
            LOG_ERROR("[Client] Failed to create host.");
            return;
        }
        ENetAddress enetAddr;
        if (enet_address_set_host(&enetAddr, address.c_str()) != 0) {
            LOG_ERROR("[Client] Failed to resolve address: {}", address);
            return;
        }
        enetAddr.port = port;
        m_ServerPeer = enet_host_connect(m_ClientHost, &enetAddr, 2, 0);
        if (!m_ServerPeer) {
            LOG_ERROR("[Client] No available peers.");
            return;
        }
        ENetEvent event;
        if (enet_host_service(m_ClientHost, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
            LOG_INFO("[Client] Connected to {}:{}", address, port);
            m_IsConnected = true;
        } else {
            enet_peer_reset(m_ServerPeer);
            LOG_ERROR("[Client] Connection to {}:{} failed.", address, port);
        }
    }

    void NetworkClient::Disconnect() {
        if (m_IsConnected && m_ServerPeer) {
            enet_peer_disconnect(m_ServerPeer, 0);
            ENetEvent event;
            while (enet_host_service(m_ClientHost, &event, 3000) > 0) {
                if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
                else if (event.type == ENET_EVENT_TYPE_DISCONNECT) break;
            }
        }
        if (m_ClientHost) {
            enet_host_destroy(m_ClientHost);
            m_ClientHost = nullptr;
        }
        enet_deinitialize();
        m_IsConnected = false;
    }

    void NetworkClient::Tick(Registry &registry,
                             PhysicsWorld *physics,
                             const UserCmd &cmd,
                             f32 deltaTime) {
        if (!m_IsConnected || !m_ServerPeer) return;
        m_ClientTime += deltaTime;

        flatbuffers::FlatBufferBuilder builder(256);
        auto clientInput = Networking::CreateClientInput(
            builder,
            cmd.MoveForward,
            cmd.MoveRight,
            cmd.ViewYaw,
            cmd.ViewPitch,
            cmd.Buttons);
        auto msg = Networking::CreateMessage(builder, Networking::Payload_ClientInput, clientInput.Union());
        builder.Finish(msg);
        ENetPacket *pkt = enet_packet_create(builder.GetBufferPointer(), builder.GetSize(), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(m_ServerPeer, 0, pkt);

        // Receive
        ENetEvent event;
        while (enet_host_service(m_ClientHost, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_RECEIVE: {
                    auto recvMsg = Networking::GetMessage(event.packet->data);
                    if (recvMsg->payload_type() == Networking::Payload_AssignPlayer) {
                        auto assign = recvMsg->payload_as_AssignPlayer();
                        m_ServerPlayerEntityId = static_cast<Entity>(assign->entity_id());
                        if (m_LocalPlayerEntityId != NULL_ENTITY && physics &&
                            registry.HasComponent<RigidBodyComponent>(m_LocalPlayerEntityId)) {
                            auto &rb = registry.GetComponent<RigidBodyComponent>(m_LocalPlayerEntityId);
                            const Vec3 spawnPos{
                                assign->position()->x(), assign->position()->y(), assign->position()->z()
                            };
                            physics->SetBodyPosition(rb.BodyId, spawnPos);
                            if (registry.HasComponent<TransformComponent>(m_LocalPlayerEntityId))
                                registry.GetComponent<TransformComponent>(m_LocalPlayerEntityId).Position = spawnPos;
                            m_LastServerPosition = spawnPos;
                        }
                        LOG_INFO("[Client] Assigned server entity {}", m_ServerPlayerEntityId);
                    } else if (recvMsg->payload_type() == Networking::Payload_SnapshotPacket) {
                        ProcessServerSnapshot(event.packet->data, event.packet->dataLength, registry, physics);
                    }
                    enet_packet_destroy(event.packet);
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                    LOG_INFO("[Client] Disconnected from server.");
                    m_IsConnected = false;
                    m_ServerPeer = nullptr;
                    break;
                default: break;
            }
        }

        ApplyInterpolation(registry, m_ClientTime);
    }

    void NetworkClient::ProcessServerSnapshot(const void *data, size_t,
                                              Registry &registry,
                                              PhysicsWorld *physics) {
        auto recvMsg = Networking::GetMessage(data);
        if (recvMsg->payload_type() != Networking::Payload_SnapshotPacket) return;
        auto snapshot = recvMsg->payload_as_SnapshotPacket();
        if (!snapshot->entities()) return;

        for (const auto *entitySn: *snapshot->entities()) {
            const u32 eId = entitySn->entity_id();

            const Vec3 serverPos{entitySn->position()->x(), entitySn->position()->y(), entitySn->position()->z()};
            const Vec3 serverVel = entitySn->velocity()
                                       ? Vec3{
                                           entitySn->velocity()->x(), entitySn->velocity()->y(),
                                           entitySn->velocity()->z()
                                       }
                                       : Vec3{0.f, 0.f, 0.f};
            const Vec3 serverColor = entitySn->color()
                                         ? Vec3{entitySn->color()->x(), entitySn->color()->y(), entitySn->color()->z()}
                                         : Vec3{1.f, 1.f, 1.f};

            if (m_ServerPlayerEntityId != NULL_ENTITY && eId == m_ServerPlayerEntityId) {
                ReconcileLocalPlayer(serverPos, registry, physics);
                continue;
            }

            bool isNew = (m_InterpolationStates.find(eId) == m_InterpolationStates.end())
                         && !registry.HasComponent<TransformComponent>(static_cast<Entity>(eId));

            // Update interpolation
            auto &interp = m_InterpolationStates[eId];
            interp.previousPosition = interp.targetPosition;
            interp.previousTime = interp.targetTime;
            interp.targetPosition = serverPos;
            interp.targetVelocity = serverVel;
            interp.targetTime = m_ClientTime;
            interp.lastUpdateTime = m_ClientTime;

            if (isNew) {
                interp.previousPosition = serverPos;
                interp.previousTime = m_ClientTime;
                registry.AddComponent<TransformComponent>(
                    static_cast<Entity>(eId),
                    TransformComponent{serverPos, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}});
            }

            RemoteEntitySnapshot snap{};
            snap.entityId = eId;
            snap.position = serverPos;
            snap.velocity = serverVel;
            snap.color = serverColor;
            snap.entityType = entitySn->entity_type();
            snap.isNew = isNew;

            if (isNew) {
                if (m_OnEntitySpawned) m_OnEntitySpawned(registry, snap);
            } else {
                if (m_OnEntityUpdated) m_OnEntityUpdated(registry, snap);
            }
        }

        m_ServerTime = m_ClientTime - m_RoundTripTime * 0.5f;
    }

    void NetworkClient::ReconcileLocalPlayer(const Vec3 &serverPosition,
                                             Registry &registry,
                                             PhysicsWorld *physics) {
        if (m_LocalPlayerEntityId == NULL_ENTITY) return;
        m_LastServerPosition = serverPosition;

        Vec3 localPos = serverPosition;
        if (registry.HasComponent<TransformComponent>(m_LocalPlayerEntityId))
            localPos = registry.GetComponent<TransformComponent>(m_LocalPlayerEntityId).Position;

        const Vec3 delta = localPos - serverPosition;
        const f32 distSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;

        if (distSq > RECONCILE_THRESHOLD * RECONCILE_THRESHOLD) {
            if (physics && registry.HasComponent<RigidBodyComponent>(m_LocalPlayerEntityId)) {
                auto &rb = registry.GetComponent<RigidBodyComponent>(m_LocalPlayerEntityId);
                if (rb.IsValid()) physics->SetBodyPosition(rb.BodyId, serverPosition);
            }
            if (registry.HasComponent<TransformComponent>(m_LocalPlayerEntityId))
                registry.GetComponent<TransformComponent>(m_LocalPlayerEntityId).Position = serverPosition;
        }
    }

    void NetworkClient::ApplyInterpolation(Registry &registry, f32 currentTime) {
        const f32 renderTime = currentTime - INTERPOLATION_DELAY;
        for (auto &[entityId, interp]: m_InterpolationStates) {
            if (currentTime - interp.lastUpdateTime > 1.f) continue;
            if (m_LocalPlayerEntityId != NULL_ENTITY && entityId == static_cast<u32>(m_LocalPlayerEntityId)) continue;
            if (!registry.HasComponent<TransformComponent>(static_cast<Entity>(entityId))) continue;

            Vec3 interpolatedPos;
            const f32 timeDiff = interp.targetTime - interp.previousTime;
            if (timeDiff > 0.0001f) {
                const f32 t = std::clamp((renderTime - interp.previousTime) / timeDiff, 0.f, 1.f);
                interpolatedPos = glm::mix(interp.previousPosition, interp.targetPosition, t);
            } else {
                interpolatedPos = interp.targetPosition + interp.targetVelocity * (currentTime - interp.targetTime);
            }
            registry.GetComponent<TransformComponent>(static_cast<Entity>(entityId)).Position = interpolatedPos;
        }
    }

    Vec3 NetworkClient::GetInterpolatedPosition(u32 entityId, f32 currentTime) {
        auto it = m_InterpolationStates.find(entityId);
        if (it == m_InterpolationStates.end()) return Vec3(0.f);
        const auto &interp = it->second;
        const f32 renderTime = currentTime - INTERPOLATION_DELAY;
        const f32 timeDiff = interp.targetTime - interp.previousTime;
        if (timeDiff <= 0.0001f)
            return interp.targetPosition + interp.targetVelocity * (currentTime - interp.targetTime);
        const f32 t = std::clamp((renderTime - interp.previousTime) / timeDiff, 0.f, 1.f);
        return glm::mix(interp.previousPosition, interp.targetPosition, t);
    }
} // namespace Engine
