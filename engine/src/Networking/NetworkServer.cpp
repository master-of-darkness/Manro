#include <Manro/Networking/NetworkServer.h>
#include <Manro/ECS/Registry.h>
#include <Manro/Core/Components.h>
#include <Manro/Core/Logger.h>
#include <Manro/Physics/PhysicsWorld.h>

namespace Manro {
    NetworkServer::NetworkServer(u16 port) : m_Port(port) {
        // TODO: initialize networking
        LOG_INFO("[Server] Listening on UDP: {}", m_Port);
    }

    NetworkServer::~NetworkServer() {
        // TODO: networking
        LOG_INFO("[Server] Stopped.");
    }

    void NetworkServer::Tick(Registry &registry, PhysicsWorld &physics, float deltaTime) {
        // TODO: implement server tick
    }

    void NetworkServer::BroadcastSnapshot(Registry &registry, PhysicsWorld &physics) {
        // TODO: implement snapshot broadcast
    }

    void NetworkServer::ValidateAndStoreInput(u32 clientId, const Networking::ClientInput *inp) {
        // TODO: implement input validation
    }

    void NetworkServer::HandleNewConnection(_ENetPeer *peer) {
        // TODO: implement new connection handling
    }

    void NetworkServer::HandleDisconnection(_ENetPeer *peer, Registry &registry) {
        // TODO: implement disconnection handling
    }
} // namespace Manro
