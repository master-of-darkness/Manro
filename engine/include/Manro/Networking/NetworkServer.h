#pragma once

#include <Manro/Core/Types.h>
#include <Manro/ECS/Entity.h>
#include <unordered_map>
#include <functional>

struct _ENetHost;
struct _ENetPeer;

namespace Manro {
    class Registry;

    class PhysicsWorld;

    namespace Networking {
        struct ClientInput;
    }

    class NetworkServer {
    public:
        explicit NetworkServer(u16 port);

        ~NetworkServer();

        NetworkServer(const NetworkServer &) = delete;

        NetworkServer &operator=(const NetworkServer &) = delete;

        void Tick(Registry &registry,
                  PhysicsWorld &physics,
                  float deltaTime);

        void BroadcastSnapshot(Registry &registry, PhysicsWorld &physics);

        f32 GetServerTime() const { return m_ServerTime; }

    private:
        void HandleNewConnection(_ENetPeer *peer);

        void HandleDisconnection(_ENetPeer *peer, Registry &registry);

        void ValidateAndStoreInput(u32 clientId, const Networking::ClientInput *inp);

        u16 m_Port{0};

        f32 m_ServerTime{0.f};
        static constexpr f32 SNAPSHOT_RATE = 0.05f;
    };
} // namespace Manro
