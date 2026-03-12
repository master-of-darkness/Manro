#include "Scene.h"
#include <Manro/Scene/Scene.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Networking/NetworkServer.h>
#include <Manro/Networking/NetworkClient.h>
#include <Manro/Core/Components.h>
#include <Manro/Physics/PhysicsWorld.h>
#include <Manro/Render/Renderer.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Manro/Core/Logger.h>
#include "CharacterMovementController.h"

WorldScene::WorldScene(Manro::Renderer &r) : m_Renderer(r) {
}

void WorldScene::OnCreate() {
    m_Registry.RegisterComponent<Manro::TransformComponent>();
    m_Registry.RegisterComponent<Manro::MeshRenderComponent>();
    m_Registry.RegisterComponent<Manro::PlayerControllerComponent>();
    m_Registry.RegisterComponent<Manro::CameraComponent>();
    m_Registry.RegisterComponent<Manro::ColorComponent>();
    m_Registry.RegisterComponent<Manro::JumpStateComponent>();
    m_Registry.RegisterComponent<Manro::StaticMeshTag>();
    m_Registry.RegisterComponent<Manro::RigidBodyComponent>();

    LoadMesh("crate", "assets/models/cube.obj");
    LoadMesh("ground", "assets/models/ground.obj");
    LoadMesh("cone", "assets/models/Cone.obj");

    // Local player
    m_Player = CreateActor({0, 0, 0}, "cone", Manro::PhysicsBodyType::Dynamic);
    m_Registry.AddComponent<Manro::PlayerControllerComponent>(m_Player, {5.f, 2.f});
    m_Registry.AddComponent<Manro::CameraComponent>(m_Player, {60.f, 0.1f, 500.f, -90.f, -10.f});
    m_Registry.AddComponent<Manro::ColorComponent>(m_Player, {{1.f, 0.f, 0.f}});
    m_Registry.AddComponent<Manro::JumpStateComponent>(m_Player, {});
    m_MovementController = std::make_unique<CharacterMovementController>(m_Registry);

    // Ground
    Manro::Entity g = CreateActor({0, -0.5f, 0}, "ground", Manro::PhysicsBodyType::Static, {40, 1, 40});
    m_Registry.AddComponent<Manro::StaticMeshTag>(g, {});

    // Props
    for (const auto &p: {Manro::Vec3{5, 0.5f, 5}, Manro::Vec3{-5, 0.5f, 5}, Manro::Vec3{0, 0.5f, -8}}) {
        Manro::Entity e = CreateActor(p, "crate", Manro::PhysicsBodyType::Dynamic, {2, 2, 2});
        m_Registry.AddComponent<Manro::ColorComponent>(e, {Manro::Vec3{1.f, 1.f, 1.f}});
        m_Props.push_back(e);
    }
}

void WorldScene::RegisterServerCallbacks(Manro::NetworkServer *server) {
    if (!server) return;
    server->SetSpawnPlayerCallback([this](Manro::u32 id) { return SpawnRemotePlayer(id); });
    server->SetDespawnPlayerCallback([this](Manro::u32 id, Manro::Entity e) { DespawnRemotePlayer(id, e); });
}

void WorldScene::RegisterClientCallbacks(Manro::NetworkClient *client) {
    if (!client) return;

    client->SetOnEntitySpawned([this](Manro::Registry &reg, const Manro::RemoteEntitySnapshot &snap) {
        const std::string &meshName = (snap.entityType == 1) ? "cone" : "crate";
        auto meshIt = m_MeshIds.find(meshName);
        if (meshIt != m_MeshIds.end())
            reg.AddComponent<Manro::MeshRenderComponent>(
                static_cast<Manro::Entity>(snap.entityId),
                Manro::MeshRenderComponent{meshIt->second});
        reg.AddComponent<Manro::ColorComponent>(
            static_cast<Manro::Entity>(snap.entityId),
            Manro::ColorComponent{snap.color});
    });

    client->SetOnEntityUpdated([this](Manro::Registry &reg, const Manro::RemoteEntitySnapshot &snap) {
        if (reg.HasComponent<Manro::ColorComponent>(static_cast<Manro::Entity>(snap.entityId)))
            reg.GetComponent<Manro::ColorComponent>(static_cast<Manro::Entity>(snap.entityId)).Color = snap.color;
    });
}

void WorldScene::OnUpdate(float dt, const Manro::UserCmd &cmd) {
    m_MovementController->ProcessMovement({
        &m_Physics,
        m_Registry.GetComponent<Manro::RigidBodyComponent>(m_Player).BodyId,
        &cmd,
        dt
    });

    m_Physics.Step(dt);
    SyncPhysics();
}

void WorldScene::OnRender(Manro::Renderer &r) {
    auto &cam = m_Registry.GetComponent<Manro::CameraComponent>(m_Player);
    auto &t = m_Registry.GetComponent<Manro::TransformComponent>(m_Player);

    Manro::Vec3 eye = t.Position + Manro::Vec3{0, 1.6f, 0};
    float yR = glm::radians(cam.Yaw), pR = glm::radians(cam.Pitch);
    Manro::Vec3 front = glm::normalize(Manro::Vec3{cos(pR) * cos(yR), sin(pR), cos(pR) * sin(yR)});

    const float aspect = r.GetAspectRatio();
    Manro::Mat4 proj = glm::perspective(glm::radians(cam.Fov), aspect, cam.NearClip, cam.FarClip);
    Manro::Mat4 view = glm::lookAt(eye, eye + front, {0, 1, 0});
    r.SetViewProjection(view, proj);

    auto meshes = m_Registry.GetComponentArray<Manro::MeshRenderComponent>();
    for (size_t i = 0; i < meshes->GetSize(); ++i) {
        Manro::Entity e = meshes->GetDenseToEntityMap()[i];
        auto &mc = meshes->GetDenseArray()[i];
        if (!m_Registry.HasComponent<Manro::TransformComponent>(e)) continue;
        auto &tr = m_Registry.GetComponent<Manro::TransformComponent>(e);

        Manro::Mat4 model = glm::scale(glm::translate(Manro::Mat4(1.f), tr.Position), tr.Scale);

        auto texIt = m_TextureIds.find(mc.MeshId);
        r.BindTexture(texIt != m_TextureIds.end() ? texIt->second : Manro::TextureHandle{0});

        r.SetTintColor(m_Registry.HasComponent<Manro::ColorComponent>(e)
                           ? m_Registry.GetComponent<Manro::ColorComponent>(e).Color
                           : Manro::Vec3{1.f, 1.f, 1.f});

        r.DrawMesh(mc.MeshId, model);
    }
    r.SetTintColor({1.f, 1.f, 1.f});
}

void WorldScene::OnDestroy() {
}

void WorldScene::SetViewAngles(float yaw, float pitch) {
    if (!m_Registry.HasComponent<Manro::CameraComponent>(m_Player)) return;
    auto &cam = m_Registry.GetComponent<Manro::CameraComponent>(m_Player);
    cam.Yaw = yaw;
    cam.Pitch = pitch;
}

Manro::Entity WorldScene::SpawnRemotePlayer(Manro::u32 clientId) {
    float offset = static_cast<float>(clientId) * 2.f;
    Manro::Entity e = CreateActor({offset, 0.f, 0.f}, "cone", Manro::PhysicsBodyType::Dynamic);
    static const Manro::Vec3 palette[] = {
        {0.2f, 0.6f, 1.f}, {1.f, 0.4f, 0.1f}, {0.2f, 0.9f, 0.3f}, {0.9f, 0.2f, 0.8f}, {1.f, 0.9f, 0.1f}
    };
    m_Registry.AddComponent<Manro::ColorComponent>(e, {palette[clientId % 5]});
    LOG_INFO("[WorldScene] Remote player entity {} for client {}", e, clientId);
    return e;
}

void WorldScene::DespawnRemotePlayer(Manro::u32, Manro::Entity entity) {
    if (entity == Manro::NULL_ENTITY) return;
    m_Registry.DestroyEntity(entity);
}

Manro::Entity WorldScene::CreateActor(Manro::Vec3 pos, const std::string &meshName,
                                       Manro::PhysicsBodyType type, Manro::Vec3 s) {
    Manro::Entity e = m_Registry.CreateEntity();
    m_Registry.AddComponent<Manro::TransformComponent>(e, {pos, {0, 0, 0}, s});
    m_Registry.AddComponent<Manro::MeshRenderComponent>(e, {m_MeshIds[meshName]});

    float hm = (meshName == "crate") ? 1.f : (meshName == "cone") ? 0.8f : 1.f;
    Manro::Vec3 colSize = s * hm;

    Manro::RigidBodyComponent rb;
    rb.Type = type;
    if (type == Manro::PhysicsBodyType::Static) rb.BodyId = m_Physics.AddStaticBox(pos, colSize * 0.5f);
    else if (type == Manro::PhysicsBodyType::Dynamic) rb.BodyId = m_Physics.AddDynamicBox(pos, colSize * 0.5f, 5.f);
    else if (type == Manro::PhysicsBodyType::Kinematic)
        rb.BodyId = m_Physics.AddKinematicCapsule(
            pos, 0.6f * hm, 0.5f * hm);

    m_Registry.AddComponent<Manro::RigidBodyComponent>(e, rb);

    if (rb.BodyId != Manro::kInvalidBodyHandle)
        m_Physics.SetBodyUserData(rb.BodyId, static_cast<Manro::u32>(e));

    return e;
}

void WorldScene::SyncPhysics() {
    m_Physics.ForEachDynamicBody([this](Manro::u32 entityId, const Manro::Vec3 &pos, const Manro::Vec3 &vel) {
        Manro::Entity e = static_cast<Manro::Entity>(entityId);
        if (m_Registry.HasComponent<Manro::TransformComponent>(e))
            m_Registry.GetComponent<Manro::TransformComponent>(e).Position = pos;
        if (m_Registry.HasComponent<Manro::RigidBodyComponent>(e))
            m_Registry.GetComponent<Manro::RigidBodyComponent>(e).Velocity = vel;
    });
}

void WorldScene::LoadMesh(const std::string &name, const std::string &path) {
    Manro::ModelData data;
    if (Manro::ModelLoader::Load(path, data)) {
        Manro::MeshHandle mid = m_Renderer.UploadMesh(data);
        m_MeshIds[name] = mid;
        if (!data.diffuseTexturePath.empty()) {
            Manro::TextureData tData;
            if (Manro::TextureLoader::Load(data.diffuseTexturePath, tData))
                m_TextureIds[mid] = m_Renderer.UploadTexture(tData);
        }
    }
}
