#include "Sponza.h"

#include <Render/ModelLoader.h>
#include <Render/FBXLoader.h>
#include <Render/TextureLoader.h>
#include <Core/Logger.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

static constexpr const char *kSponzaPath = "assets/models/sponza.obj";
static constexpr const char *kBistroPaths[] = {
    "assets/models/BistroExterior.fbx",
    "assets/models/BistroInterior.fbx",
    "assets/models/BistroInterior_Wine.fbx",
};
static constexpr float kFov = 100.f;
static constexpr float kNearZ = 1.f;
static constexpr float kFarZ = 10000.f;
static constexpr Engine::u32 kWindowWidth = 1920;
static constexpr Engine::u32 kWindowHeight = 1080;

Engine::Vec3 FlyCamera::Forward() const {
    const float yR = glm::radians(Yaw);
    const float pR = glm::radians(Pitch);
    return glm::normalize(Engine::Vec3{
        cosf(pR) * cosf(yR),
        sinf(pR),
        cosf(pR) * sinf(yR)
    });
}

void FlyCamera::Update(const Engine::InputManager &input, float dt) {
    using K = Engine::Key;

    Engine::RawMouseDelta delta = const_cast<Engine::InputManager &>(input).ConsumeMouseDelta();
    Yaw += delta.x * MouseSensitivity;
    Pitch = std::clamp(Pitch - delta.y * MouseSensitivity, -89.f, 89.f);

    const Engine::Vec3 fwd = Forward();
    const Engine::Vec3 right = glm::normalize(glm::cross(fwd, Engine::Vec3{0.f, 1.f, 0.f}));
    const Engine::Vec3 up = {0.f, 1.f, 0.f};
    const float speed = input.IsKeyDown(K::LeftShift) ? SprintSpeed : NormalSpeed;

    Engine::Vec3 move{0.f};
    if (input.IsKeyDown(K::W)) move += fwd;
    if (input.IsKeyDown(K::S)) move -= fwd;
    if (input.IsKeyDown(K::D)) move += right;
    if (input.IsKeyDown(K::A)) move -= right;
    if (input.IsKeyDown(K::E)) move += up;
    if (input.IsKeyDown(K::Q)) move -= up;

    if (glm::length(move) > 0.001f)
        Position += glm::normalize(move) * speed * dt;
}

Engine::Mat4 FlyCamera::ViewProj(float fovDeg, float aspect, float nearZ, float farZ) const {
    Engine::Mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, nearZ, farZ);
    proj[1][1] *= -1;
    const Engine::Vec3 fwd = Forward();
    const Engine::Mat4 view = glm::lookAt(Position, Position + fwd, {0.f, 1.f, 0.f});
    return proj * view;
}

void Sponza::Initialize() {
    m_Engine.Initialize(true);

    auto &wm = m_Engine.GetPlatform().GetWindowManager();
    Engine::WindowDesc desc;
    desc.Title = (m_SceneType == SceneType::Bistro) ? "Bistro PBR" : "Sponza PBR";
    desc.Width = kWindowWidth;
    desc.Height = kWindowHeight;
    m_Window = wm.AddWindow(desc);
    if (m_Window == Engine::kInvalidWindow)
        throw std::runtime_error("[SponzaTest] Failed to create window.");

    wm.Get(m_Window)->SetEventCallback(
        [this](Engine::WindowEvent ev, Engine::u32 w, Engine::u32 h) {
            if (ev == Engine::WindowEvent::Close) m_IsRunning = false;
            else if (ev == Engine::WindowEvent::Resized && m_Renderer)
                m_Renderer->OnResize(w, h);
        });

    m_InputManager.SetBackend(&m_InputBackend);
    wm.Get(m_Window)->CaptureMouse(true);
    wm.Get(m_Window)->ShowCursor(false);

    m_Renderer = Engine::CreateScope<Engine::Renderer>();
    m_Renderer->Initialize(wm.Get(m_Window), kWindowWidth, kWindowHeight, VK_SAMPLE_COUNT_8_BIT);
    LOG_INFO("[SponzaTest] Renderer initialized.");

    LoadScene();

    m_IsRunning = true;
    m_LastFrameTime = std::chrono::high_resolution_clock::now();
    LOG_INFO("[SponzaTest] Ready.  WASD=move  Mouse=look  Shift=sprint  Q/E=up/down  Escape=quit");
}

static Engine::u32 LoadOrCacheTexture(
    const std::string &path,
    std::unordered_map<std::string, Engine::u32> &cache,
    Engine::Renderer *renderer) {
    if (path.empty()) return 0;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    Engine::TextureData td;
    if (Engine::TextureLoader::Load(path, td)) {
        Engine::u32 id = renderer->UploadTexture(td);
        cache[path] = id;
        return id;
    }
    return 0;
}

void Sponza::LoadScene() {
    std::vector<const char *> paths;
    if (m_SceneType == SceneType::Bistro) {
        for (const char *p : kBistroPaths)
            paths.push_back(p);
    } else {
        paths.push_back(kSponzaPath);
    }

    std::unordered_map<std::string, Engine::u32> textureCache;

    for (const char *path : paths) {
        std::vector<Engine::PBRSubMeshData> subMeshData;
        bool ok = false;

        if (m_SceneType == SceneType::Bistro) {
            ok = Engine::FBXLoader::LoadPBRSubMeshes(path, subMeshData,
                                                      Engine::AxisRemap::ZUpToYUp());
        }
        // TODO: OBJ PBR loading for Sponza

        if (!ok) {
            LOG_ERROR("[SponzaTest] Failed to load '{}'. Skipping.", path);
            continue;
        }

        for (auto &sd : subMeshData) {
            if (sd.vertices.empty()) continue;

            PBRSubMesh sm;
            sm.meshId = m_Renderer->UploadPBRMesh(sd);

            sm.material.albedoTextureId = LoadOrCacheTexture(
                sd.albedoTexturePath, textureCache, m_Renderer.get());
            sm.material.normalTextureId = LoadOrCacheTexture(
                sd.normalTexturePath, textureCache, m_Renderer.get());
            sm.material.specularTextureId = LoadOrCacheTexture(
                sd.specularTexturePath, textureCache, m_Renderer.get());

            m_Renderer->BuildPBRMaterialDescriptor(sm.material);
            m_SubMeshes.push_back(sm);
        }

        LOG_INFO("[SponzaTest] Loaded '{}' — {} sub-meshes total so far", path, m_SubMeshes.size());
    }

    LOG_INFO("[SponzaTest] Scene loaded: {} sub-meshes, {} unique textures",
             m_SubMeshes.size(), textureCache.size());
}

void Sponza::Run() {
    auto &platform = m_Engine.GetPlatform();

    while (m_IsRunning) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - m_LastFrameTime).count();
        m_LastFrameTime = now;
        if (dt > 0.1f) dt = 0.1f;

        if (!platform.PollEvents(&m_InputManager))
            m_IsRunning = false;

        if (m_InputManager.IsKeyDown(Engine::Key::Escape))
            m_IsRunning = false;

        m_Camera.Update(m_InputManager, dt);
        Render(dt);
    }
}

void Sponza::Render(float dt) {
    if (!m_Renderer) return;

    if (!m_Renderer->BeginFrame())
        return;
    m_Renderer->BeginRenderPass({0.05f, 0.05f, 0.08f, 1.f});

    const float aspect = m_Renderer->GetAspectRatio();
    const Engine::Mat4 vp = m_Camera.ViewProj(kFov, aspect, kNearZ, kFarZ);
    const Engine::Mat4 model = Engine::Mat4(1.f);

    Engine::LightDataGPU lights{};
    lights.dirLightDirection = glm::normalize(Engine::Vec3{-0.5f, -1.0f, -0.3f});
    lights.dirLightColor = {1.0f, 0.98f, 0.95f};
    lights.dirLightIntensity = 1.5f;
    lights.cameraPos = m_Camera.Position;
    lights.ambientColor = {0.6f, 0.65f, 0.8f};
    lights.ambientIntensity = 0.15f;
    lights.numPointLights = 0;
    m_Renderer->UpdateLights(lights);

    m_Renderer->BeginPBRPass();

    for (const auto &sm : m_SubMeshes) {
        m_Renderer->BindPBRMaterial(sm.material);
        m_Renderer->DrawPBRMesh(sm.meshId, vp * model, model);
    }

    m_Renderer->EndRenderPass();
    m_Renderer->EndFrameAndPresent();
}

void Sponza::Shutdown() {
    m_Renderer.reset();
    m_Engine.Shutdown();
    m_IsRunning = false;
}
