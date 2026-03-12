#include "Sponza.h"

#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Core/Logger.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

static constexpr const char *kSponzaPath = "assets/models/sponza.obj";
static constexpr float kFov = 100.f;
static constexpr float kNearZ = 1.f;
static constexpr float kFarZ = 10000.f;
static constexpr Manro::u32 kWindowWidth = 1920;
static constexpr Manro::u32 kWindowHeight = 1080;

Manro::Vec3 FlyCamera::Forward() const {
    const float yR = glm::radians(Yaw);
    const float pR = glm::radians(Pitch);
    return glm::normalize(Manro::Vec3{
        cosf(pR) * cosf(yR),
        sinf(pR),
        cosf(pR) * sinf(yR)
    });
}

void FlyCamera::Update(const Manro::InputManager &input, float dt) {
    using K = Manro::Key;

    Manro::RawMouseDelta delta = const_cast<Manro::InputManager &>(input).ConsumeMouseDelta();
    Yaw += delta.x * MouseSensitivity;
    Pitch = std::clamp(Pitch - delta.y * MouseSensitivity, -89.f, 89.f);

    const Manro::Vec3 fwd = Forward();
    const Manro::Vec3 right = glm::normalize(glm::cross(fwd, Manro::Vec3{0.f, 1.f, 0.f}));
    const Manro::Vec3 up = {0.f, 1.f, 0.f};
    const float speed = input.IsKeyDown(K::LeftShift) ? SprintSpeed : NormalSpeed;

    Manro::Vec3 move{0.f};
    if (input.IsKeyDown(K::W)) move += fwd;
    if (input.IsKeyDown(K::S)) move -= fwd;
    if (input.IsKeyDown(K::D)) move += right;
    if (input.IsKeyDown(K::A)) move -= right;
    if (input.IsKeyDown(K::E)) move += up;
    if (input.IsKeyDown(K::Q)) move -= up;

    if (glm::length(move) > 0.001f)
        Position += glm::normalize(move) * speed * dt;
}

Manro::Mat4 FlyCamera::View() const {
    const Manro::Vec3 fwd = Forward();
    return glm::lookAt(Position, Position + fwd, {0.f, 1.f, 0.f});
}

Manro::Mat4 FlyCamera::Projection(float fovDeg, float aspect, float nearZ, float farZ) const {
    return glm::perspective(glm::radians(fovDeg), aspect, nearZ, farZ);
}

Manro::Mat4 FlyCamera::ViewProj(float fovDeg, float aspect, float nearZ, float farZ) const {
    Manro::Mat4 proj = Projection(fovDeg, aspect, nearZ, farZ);
    proj[1][1] *= -1;
    return proj * View();
}

void Sponza::Initialize() {
    auto &wm = m_Engine.GetPlatform().GetWindowManager();
    Manro::WindowDesc desc;
    desc.Title = (m_SceneType == SceneType::Bistro) ? "Bistro Test" : "Sponza Test";
    desc.Width = kWindowWidth;
    desc.Height = kWindowHeight;
    m_Window = wm.AddWindow(desc);
    if (m_Window == Manro::kInvalidWindow)
        throw std::runtime_error("[SponzaTest] Failed to create window.");

    auto *window = wm.Get(m_Window);
    window->SetEventCallback(
        [this](Manro::WindowEvent ev, Manro::u32 w, Manro::u32 h) {
            if (ev == Manro::WindowEvent::Close) m_IsRunning = false;
            else if (ev == Manro::WindowEvent::Resized && m_Renderer)
                m_Renderer->OnResize(w, h);
        });

    m_InputManager.SetBackend(&m_InputBackend);
    window->CaptureMouse(true);
    window->ShowCursor(false);

    m_Renderer = Manro::CreateScope<Manro::Renderer>(*window, kWindowWidth, kWindowHeight, VK_SAMPLE_COUNT_8_BIT);
    LOG_INFO("[SponzaTest] Renderer initialized.");

    LoadScene();

    m_IsRunning = true;
    m_LastFrameTime = std::chrono::high_resolution_clock::now();
    LOG_INFO("[SponzaTest] Ready.  WASD=move  Mouse=look  Shift=sprint  Q/E=up/down  Escape=quit");
}


void Sponza::LoadScene() {
    std::vector<const char *> paths;
    paths.push_back(kSponzaPath);

    std::unordered_map<std::string, Manro::TextureHandle> textureCache;

    for (const char *path: paths) {
        std::vector<Manro::SubMeshData> subMeshData;
        bool ok = false;
        ok = Manro::ModelLoader::LoadSubMeshes(path, subMeshData);
        if (!ok) {
            LOG_ERROR("[SponzaTest] Failed to load '{}'. Skipping.", path);
            continue;
        }

        for (auto &sd: subMeshData) {
            if (sd.vertices.empty()) continue;

            Manro::ModelData md;
            md.vertices = std::move(sd.vertices);
            md.indices = std::move(sd.indices);
            md.diffuseTexturePath = sd.diffuseTexturePath;

            SubMesh sm;
            sm.meshId = m_Renderer->UploadMesh(md);
            sm.material = m_Renderer->CreateMaterialInstance(m_Renderer->GetDefaultMaterial());

            if (!sd.diffuseTexturePath.empty()) {
                auto cacheIt = textureCache.find(sd.diffuseTexturePath);
                if (cacheIt != textureCache.end()) {
                    sm.material->SetTexture(cacheIt->second);
                } else {
                    Manro::TextureData td;
                    if (Manro::TextureLoader::Load(sd.diffuseTexturePath, td)) {
                        Manro::TextureHandle tex = m_Renderer->UploadTexture(td);
                        sm.material->SetTexture(tex);
                        textureCache[sd.diffuseTexturePath] = tex;
                    }
                }
            }


            m_SubMeshes.push_back(std::move(sm));
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

        if (m_InputManager.IsKeyDown(Manro::Key::Escape))
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
    m_Renderer->SetViewProjection(m_Camera.View(), m_Camera.Projection(kFov, aspect, kNearZ, kFarZ));
 
    for (const auto &sm: m_SubMeshes) {
        m_Renderer->DrawMesh(sm.meshId, *sm.material, Manro::Mat4{1.0f});
    }

    // (Removed SetTintColor)
    m_Renderer->EndRenderPass();
    m_Renderer->EndFrameAndPresent();
}

void Sponza::Shutdown() {
    m_SubMeshes.clear();
    m_Renderer.reset();
    m_IsRunning = false;
}
