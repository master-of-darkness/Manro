#pragma once

#include <Manro/Scene/Scene.h>
#include <Manro/Core/Components.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Physics/PhysicsWorld.h>
#include <Manro/Networking/NetworkServer.h>
#include <Manro/Networking/NetworkClient.h>
#include <Manro/Input/InputAction.h>
#include <string>
#include <unordered_map>
#include <memory>

#include "CharacterMovementController.h"

class WorldScene : public Engine::Scene {
public:
    explicit WorldScene(Engine::Renderer &r);

    void OnCreate() override;

    void OnDestroy() override;

    void OnUpdate(float dt, const Engine::UserCmd &cmd) override;

    void OnRender(Engine::Renderer &r) override;

    void RegisterServerCallbacks(Engine::NetworkServer *server);

    void RegisterClientCallbacks(Engine::NetworkClient *client);

    void SetViewAngles(float yaw, float pitch);

    Engine::PhysicsWorld &GetPhysics() { return m_Physics; }
    Engine::Entity GetPlayerEntity() const { return m_Player; }
    const std::vector<Engine::Entity> &GetProps() const { return m_Props; }

    Engine::MeshHandle GetMeshId(const std::string &name) const {
        auto it = m_MeshIds.find(name);
        return it != m_MeshIds.end() ? it->second : Engine::MeshHandle{0};
    }

    CharacterMovementController &GetMovementController() { return *m_MovementController; }

private:
    Engine::Entity CreateActor(Engine::Vec3 pos, const std::string &meshName,
                               Engine::PhysicsBodyType type,
                               Engine::Vec3 scale = {1, 1, 1});

    Engine::Entity SpawnRemotePlayer(Engine::u32 clientId);

    void DespawnRemotePlayer(Engine::u32 clientId, Engine::Entity entity);

    void SyncPhysics();

    void LoadMesh(const std::string &name, const std::string &path);

    Engine::Renderer &m_Renderer;
    Engine::Entity m_Player{Engine::NULL_ENTITY};
    Engine::PhysicsWorld m_Physics;

    std::unique_ptr<CharacterMovementController> m_MovementController;

    std::vector<Engine::Entity> m_Props;
    std::unordered_map<std::string, Engine::MeshHandle> m_MeshIds;
    std::unordered_map<Engine::u32, Engine::TextureHandle> m_TextureIds;
};
