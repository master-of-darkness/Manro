#include "Scene.h"
#include <Render/ModelLoader.h>
#include <Render/TextureLoader.h>
#include <Networking/NetworkServer.h>
#include <Networking/NetworkClient.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Core/Logger.h>

WorldScene::WorldScene(Engine::Renderer *r) : m_Renderer(r) {
}

void WorldScene::OnCreate() {
    m_Registry.RegisterComponent<Engine::TransformComponent>();
    m_Registry.RegisterComponent<Engine::CameraComponent>();
    m_Registry.RegisterComponent<Engine::MeshRenderComponent>();
    m_Registry.RegisterComponent<Engine::ColorComponent>();
    m_Registry.RegisterComponent<Engine::PlayerControllerComponent>();
    m_Registry.RegisterComponent<Engine::RigidBodyComponent>();
    m_Registry.RegisterComponent<Engine::StaticMeshTag>();
    m_Registry.RegisterComponent<Engine::JumpStateComponent>();

    m_Physics.Initialize();

    LoadMesh("crate", "assets/models/cube.obj");
    LoadMesh("ground", "assets/models/ground.obj");
    LoadMesh("cone", "assets/models/Cone.obj");

    // Local player
    m_Player = CreateActor({0, 0, 0}, "cone", Engine::PhysicsBodyType::Dynamic);
    m_Registry.AddComponent<Engine::PlayerControllerComponent>(m_Player, {5.f, 2.f});
    m_Registry.AddComponent<Engine::CameraComponent>(m_Player, {60.f, 0.1f, 500.f, -90.f, -10.f});
    m_Registry.AddComponent<Engine::ColorComponent>(m_Player, {{1.f, 0.f, 0.f}});
    m_Registry.AddComponent<Engine::JumpStateComponent>(m_Player, {});
    m_MovementController = std::make_unique<CharacterMovementController>(m_Registry);

    // Ground
    Engine::Entity g = CreateActor({0, -0.5f, 0}, "ground", Engine::PhysicsBodyType::Static, {40, 1, 40});
    m_Registry.AddComponent<Engine::StaticMeshTag>(g, {});

    // Props
    for (const auto &p: {Engine::Vec3{5, 0.5f, 5}, Engine::Vec3{-5, 0.5f, 5}, Engine::Vec3{0, 0.5f, -8}}) {
        Engine::Entity e = CreateActor(p, "crate", Engine::PhysicsBodyType::Dynamic, {2, 2, 2});
        m_Registry.AddComponent<Engine::ColorComponent>(e, {Engine::Vec3{1.f, 1.f, 1.f}});
        m_Props.push_back(e);
    }
}

void WorldScene::RegisterServerCallbacks(Engine::NetworkServer *server) {
    if (!server) return;
    server->SetSpawnPlayerCallback([this](Engine::u32 id) { return SpawnRemotePlayer(id); });
    server->SetDespawnPlayerCallback([this](Engine::u32 id, Engine::Entity e) { DespawnRemotePlayer(id, e); });
}

void WorldScene::RegisterClientCallbacks(Engine::NetworkClient *client) {
    if (!client) return;

    client->SetOnEntitySpawned([this](Engine::Registry &reg, const Engine::RemoteEntitySnapshot &snap) {
        const std::string &meshName = (snap.entityType == 1) ? "cone" : "crate";
        auto meshIt = m_MeshIds.find(meshName);
        if (meshIt != m_MeshIds.end())
            reg.AddComponent<Engine::MeshRenderComponent>(
                static_cast<Engine::Entity>(snap.entityId),
                Engine::MeshRenderComponent{meshIt->second});
        reg.AddComponent<Engine::ColorComponent>(
            static_cast<Engine::Entity>(snap.entityId),
            Engine::ColorComponent{snap.color});
    });

    client->SetOnEntityUpdated([this](Engine::Registry &reg, const Engine::RemoteEntitySnapshot &snap) {
        if (reg.HasComponent<Engine::ColorComponent>(static_cast<Engine::Entity>(snap.entityId)))
            reg.GetComponent<Engine::ColorComponent>(static_cast<Engine::Entity>(snap.entityId)).Color = snap.color;
    });
}

void WorldScene::OnUpdate(float dt,
                          Engine::NetworkClient *net,
                          const Engine::UserCmd &cmd) {
    m_MovementController->ProcessMovement({
        &m_Physics,
        m_Registry.GetComponent<Engine::RigidBodyComponent>(m_Player).BodyId,
        &cmd,
        dt
    });

    m_Physics.Step(dt);
    SyncPhysics();

    if (net) net->Tick(m_Registry, &m_Physics, cmd, dt);
}

void WorldScene::OnRender(Engine::Renderer *r) {
    auto &cam = m_Registry.GetComponent<Engine::CameraComponent>(m_Player);
    auto &t = m_Registry.GetComponent<Engine::TransformComponent>(m_Player);

    Engine::Vec3 eye = t.Position + Engine::Vec3{0, 1.6f, 0};
    float yR = glm::radians(cam.Yaw), pR = glm::radians(cam.Pitch);
    Engine::Vec3 front = glm::normalize(Engine::Vec3{cos(pR) * cos(yR), sin(pR), cos(pR) * sin(yR)});

    const float aspect = r->GetAspectRatio();
    Engine::Mat4 proj = glm::perspective(glm::radians(cam.Fov), aspect, cam.NearClip, cam.FarClip);
    proj[1][1] *= -1; // TODO: wrap this directly into engine
    Engine::Mat4 vp = proj * glm::lookAt(eye, eye + front, {0, 1, 0});

    auto meshes = m_Registry.GetComponentArray<Engine::MeshRenderComponent>();
    for (size_t i = 0; i < meshes->GetSize(); ++i) {
        Engine::Entity e = meshes->GetDenseToEntityMap()[i];
        auto &mc = meshes->GetDenseArray()[i];
        if (!m_Registry.HasComponent<Engine::TransformComponent>(e)) continue;
        auto &tr = m_Registry.GetComponent<Engine::TransformComponent>(e);

        Engine::Mat4 model = glm::scale(glm::translate(Engine::Mat4(1.f), tr.Position), tr.Scale);

        auto texIt = m_TextureIds.find(mc.MeshId);
        r->BindTexture(texIt != m_TextureIds.end() ? texIt->second : 0);

        r->SetTintColor(m_Registry.HasComponent<Engine::ColorComponent>(e)
                            ? m_Registry.GetComponent<Engine::ColorComponent>(e).Color
                            : Engine::Vec3{1.f, 1.f, 1.f});

        r->DrawMesh(mc.MeshId, vp * model);
    }
    r->SetTintColor({1.f, 1.f, 1.f});
}

void WorldScene::OnDestroy() { m_Physics.Shutdown(); }

void WorldScene::SetViewAngles(float yaw, float pitch) {
    if (!m_Registry.HasComponent<Engine::CameraComponent>(m_Player)) return;
    auto &cam = m_Registry.GetComponent<Engine::CameraComponent>(m_Player);
    cam.Yaw = yaw;
    cam.Pitch = pitch;
}

Engine::Entity WorldScene::SpawnRemotePlayer(Engine::u32 clientId) {
    float offset = static_cast<float>(clientId) * 2.f;
    Engine::Entity e = CreateActor({offset, 0.f, 0.f}, "cone", Engine::PhysicsBodyType::Dynamic);
    static const Engine::Vec3 palette[] = {
        {0.2f, 0.6f, 1.f}, {1.f, 0.4f, 0.1f}, {0.2f, 0.9f, 0.3f}, {0.9f, 0.2f, 0.8f}, {1.f, 0.9f, 0.1f}
    };
    m_Registry.AddComponent<Engine::ColorComponent>(e, {palette[clientId % 5]});
    LOG_INFO("[WorldScene] Remote player entity {} for client {}", e, clientId);
    return e;
}

void WorldScene::DespawnRemotePlayer(Engine::u32, Engine::Entity entity) {
    if (entity == Engine::NULL_ENTITY) return;
    m_Registry.DestroyEntity(entity);
}

Engine::Entity WorldScene::CreateActor(Engine::Vec3 pos, const std::string &meshName,
                                       Engine::PhysicsBodyType type, Engine::Vec3 s) {
    Engine::Entity e = m_Registry.CreateEntity();
    m_Registry.AddComponent<Engine::TransformComponent>(e, {pos, {0, 0, 0}, s});
    m_Registry.AddComponent<Engine::MeshRenderComponent>(e, {m_MeshIds[meshName]});

    float hm = (meshName == "crate") ? 1.f : (meshName == "cone") ? 0.8f : 1.f;
    Engine::Vec3 colSize = s * hm;

    Engine::RigidBodyComponent rb;
    rb.Type = type;
    if (type == Engine::PhysicsBodyType::Static) rb.BodyId = m_Physics.AddStaticBox(pos, colSize * 0.5f);
    else if (type == Engine::PhysicsBodyType::Dynamic) rb.BodyId = m_Physics.AddDynamicBox(pos, colSize * 0.5f, 5.f);
    else if (type == Engine::PhysicsBodyType::Kinematic)
        rb.BodyId = m_Physics.AddKinematicCapsule(
            pos, 0.6f * hm, 0.5f * hm);

    m_Registry.AddComponent<Engine::RigidBodyComponent>(e, rb);

    if (rb.BodyId != Engine::kInvalidBodyHandle)
        m_Physics.SetBodyUserData(rb.BodyId, static_cast<Engine::u32>(e));

    return e;
}

void WorldScene::SyncPhysics() {
    m_Physics.ForEachDynamicBody([this](Engine::u32 entityId, const Engine::Vec3 &pos, const Engine::Vec3 &vel) {
        Engine::Entity e = static_cast<Engine::Entity>(entityId);
        if (m_Registry.HasComponent<Engine::TransformComponent>(e))
            m_Registry.GetComponent<Engine::TransformComponent>(e).Position = pos;
        if (m_Registry.HasComponent<Engine::RigidBodyComponent>(e))
            m_Registry.GetComponent<Engine::RigidBodyComponent>(e).Velocity = vel;
    });
}

void WorldScene::LoadMesh(const std::string &name, const std::string &path) {
    Engine::ModelData data;
    if (Engine::ModelLoader::Load(path, data)) {
        Engine::u32 mid = m_Renderer->UploadMesh(data);
        m_MeshIds[name] = mid;
        if (!data.diffuseTexturePath.empty()) {
            Engine::TextureData tData;
            if (Engine::TextureLoader::Load(data.diffuseTexturePath, tData))
                m_TextureIds[mid] = m_Renderer->UploadTexture(tData);
        }
    }
}
