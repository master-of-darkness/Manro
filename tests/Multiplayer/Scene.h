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

class WorldScene : public Manro::Scene {
public:
    explicit WorldScene(Manro::Renderer &r);

    void OnCreate() override;

    void OnDestroy() override;

    void OnUpdate(float dt, const Manro::UserCmd &cmd) override;

    void OnRender(Manro::Renderer &r) override;

    void RegisterServerCallbacks(Manro::NetworkServer *server);

    void RegisterClientCallbacks(Manro::NetworkClient *client);

    void SetViewAngles(float yaw, float pitch);

    Manro::PhysicsWorld &GetPhysics() { return m_Physics; }
    Manro::Entity GetPlayerEntity() const { return m_Player; }
    const std::vector<Manro::Entity> &GetProps() const { return m_Props; }

    Manro::MeshHandle GetMeshId(const std::string &name) const {
        auto it = m_MeshIds.find(name);
        return it != m_MeshIds.end() ? it->second : Manro::MeshHandle{0};
    }

    CharacterMovementController &GetMovementController() { return *m_MovementController; }

private:
    Manro::Entity CreateActor(Manro::Vec3 pos, const std::string &meshName,
                               Manro::PhysicsBodyType type,
                               Manro::Vec3 scale = {1, 1, 1});

    Manro::Entity SpawnRemotePlayer(Manro::u32 clientId);

    void DespawnRemotePlayer(Manro::u32 clientId, Manro::Entity entity);

    void SyncPhysics();

    void LoadMesh(const std::string &name, const std::string &path);

    Manro::Renderer &m_Renderer;
    Manro::Entity m_Player{Manro::NULL_ENTITY};
    Manro::PhysicsWorld m_Physics;

    std::unique_ptr<CharacterMovementController> m_MovementController;

    std::vector<Manro::Entity> m_Props;
    std::unordered_map<std::string, Manro::MeshHandle> m_MeshIds;
    std::unordered_map<Manro::u32, Manro::TextureHandle> m_TextureIds;
};
