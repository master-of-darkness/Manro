#include "Sponza.h"

#include <Render/ModelLoader.h>
#include <Render/TextureLoader.h>
#include <Core/Logger.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

static constexpr const char *kSponzaPath = "assets/models/sponza.obj";
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
    desc.Title = "Sponza Test";
    desc.Width = kWindowWidth;
    desc.Height = kWindowHeight;
    m_Window = wm.AddWindow(desc);
    if (m_Window == Engine::kInvalidWindow)
        throw std::runtime_error("[SponzaTest] Failed to create window.");

    wm.Get(m_Window)->SetEventCallback(
        [this](Engine::WindowEvent ev, Engine::u32, Engine::u32) {
            if (ev == Engine::WindowEvent::Close) m_IsRunning = false;
        });

    m_InputManager.SetBackend(&m_InputBackend);
    wm.Get(m_Window)->CaptureMouse(true);
    wm.Get(m_Window)->ShowCursor(false);

    m_Renderer = Engine::CreateScope<Engine::Renderer>();
    m_Renderer->Initialize(wm.Get(m_Window), kWindowWidth, kWindowHeight);
    LOG_INFO("[SponzaTest] Renderer initialized.");

    LoadSponza();

    m_IsRunning = true;
    m_LastFrameTime = std::chrono::high_resolution_clock::now();
    LOG_INFO("[SponzaTest] Ready.  WASD=move  Mouse=look  Shift=sprint  Q/E=up/down  Escape=quit");
}

void Sponza::LoadSponza() {
    LOG_INFO("[SponzaTest] Loading {} ...", kSponzaPath);

    std::vector<Engine::SubMeshData> subMeshData;
    if (!Engine::ModelLoader::LoadSubMeshes(kSponzaPath, subMeshData)) {
        LOG_ERROR("[SponzaTest] Failed to load Sponza. Make sure '{}' exists.", kSponzaPath);
        return;
    }

    m_SubMeshes.reserve(subMeshData.size());
    size_t totalVerts = 0;
    size_t totalIndices = 0;
    int texLoaded = 0;
    int texFailed = 0;

    for (auto &sd: subMeshData) {
        if (sd.vertices.empty()) continue;

        Engine::ModelData md;
        md.vertices = std::move(sd.vertices);
        md.indices = std::move(sd.indices);
        md.diffuseTexturePath = sd.diffuseTexturePath;

        SubMesh sm;
        sm.meshId = m_Renderer->UploadMesh(md);

        if (!sd.diffuseTexturePath.empty()) {
            Engine::TextureData td;
            if (Engine::TextureLoader::Load(sd.diffuseTexturePath, td)) {
                sm.textureId = m_Renderer->UploadTexture(td);
                ++texLoaded;
            } else {
                ++texFailed;
            }
        }

        totalVerts += md.vertices.size();
        totalIndices += md.indices.size();
        m_SubMeshes.push_back(sm);
    }
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

    m_Renderer->BeginFrame();
    m_Renderer->BeginRenderPass({0.05f, 0.05f, 0.08f, 1.f});

    const float aspect = m_Renderer->GetAspectRatio();
    const Engine::Mat4 vp = m_Camera.ViewProj(kFov, aspect, kNearZ, kFarZ);

    for (const auto &sm: m_SubMeshes) {
        m_Renderer->BindTexture(sm.textureId);
        m_Renderer->SetTintColor({1.f, 1.f, 1.f});
        m_Renderer->DrawMesh(sm.meshId, vp);
    }

    m_Renderer->SetTintColor({1.f, 1.f, 1.f});
    m_Renderer->EndRenderPass();
    m_Renderer->EndFrameAndPresent();
}

void Sponza::Shutdown() {
    m_Renderer.reset();
    m_Engine.Shutdown();
    m_IsRunning = false;
}
