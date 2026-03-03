#pragma once

#include <Scene/Scene.h>
#include <Core/Components.h>
#include <Render/Renderer.h>
#include <Physics/PhysicsWorld.h>
#include <Networking/NetworkServer.h>
#include <Networking/NetworkClient.h>
#include <Input/InputAction.h>
#include <string>
#include <unordered_map>
#include <memory>

#include "CharacterMovementController.h"

class WorldScene : public Engine::Scene {
public:
    explicit WorldScene(Engine::Renderer *r);

    void OnCreate() override;

    void OnDestroy() override;

    void OnUpdate(float dt, Engine::NetworkClient *net, const Engine::UserCmd &cmd) override;

    void OnRender(Engine::Renderer *r) override;

    void RegisterServerCallbacks(Engine::NetworkServer *server);

    void RegisterClientCallbacks(Engine::NetworkClient *client);

    void SetViewAngles(float yaw, float pitch);

    Engine::PhysicsWorld &GetPhysics() { return m_Physics; }
    Engine::Entity GetPlayerEntity() const { return m_Player; }
    const std::vector<Engine::Entity> &GetProps() const { return m_Props; }

    Engine::u32 GetMeshId(const std::string &name) const {
        auto it = m_MeshIds.find(name);
        return it != m_MeshIds.end() ? it->second : 0;
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

    Engine::Renderer *m_Renderer;
    Engine::Entity m_Player{Engine::NULL_ENTITY};
    Engine::PhysicsWorld m_Physics;

    std::unique_ptr<CharacterMovementController> m_MovementController;

    std::vector<Engine::Entity> m_Props;
    std::unordered_map<std::string, Engine::u32> m_MeshIds;
    std::unordered_map<Engine::u32, Engine::u32> m_TextureIds;
};
