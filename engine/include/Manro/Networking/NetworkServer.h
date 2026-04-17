#pragma once

#include <Manro/Core/Types.h>
#include <Manro/ECS/Entity.h>
#include <unordered_map>
#include <functional>

struct _ENetHost_t;
struct _ENetPeer_t;

namespace Manro {
    class CRegistry;

    class CPhysicsWorld;

    namespace Networking {
        struct ClientInput_t;
    }

    class CNetworkServer {
    public:
        explicit CNetworkServer(u16 port);

        ~CNetworkServer();

        CNetworkServer(const CNetworkServer &) = delete;

        CNetworkServer &operator=(const CNetworkServer &) = delete;

        void Tick(CRegistry &registry,
                  CPhysicsWorld &physics,
                  float deltaTime);

        void BroadcastSnapshot(CRegistry &registry, CPhysicsWorld &physics);

        f32 GetServerTime() const { return m_flServerTime; }

    private:
        void HandleNewConnection(_ENetPeer_t *peer);

        void HandleDisconnection(_ENetPeer_t *peer, CRegistry &registry);

        void ValidateAndStoreInput(u32 clientId, const Networking::ClientInput_t *inp);

        u16 m_unPort{0};

        f32 m_flServerTime{0.f};
        static constexpr f32 SNAPSHOT_RATE = 0.05f;
    };
} // namespace Manro
