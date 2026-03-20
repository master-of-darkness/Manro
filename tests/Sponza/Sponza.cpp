#include "Sponza.h"

#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

static constexpr const char *kSponzaPath = "models/gltf/Sponza.gltf";
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
    desc.Title = "Sponza Test";
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

    Manro::VirtualFS::Get().SetBaseDir(MANRO_ASSETS_DIR);
    Manro::RegisterEmbeddedShaders();

    m_Renderer = Manro::CreateScope<Manro::Renderer>(*window, kWindowWidth, kWindowHeight, VK_SAMPLE_COUNT_8_BIT);
    LOG_INFO("[SponzaTest] Renderer initialized.");

    LoadScene();

    m_IsRunning = true;
    m_LastFrameTime = std::chrono::high_resolution_clock::now();
    LOG_INFO("[SponzaTest] Ready.  WASD=move  Mouse=look  Shift=sprint  Q/E=up/down  Escape=quit");
}


void Sponza::LoadScene() {
    auto models = Manro::Model::Load({kSponzaPath}, *m_Renderer, m_Engine.GetJobSystem());
    if (models.empty() || !models[0]) {
        LOG_ERROR("[SponzaTest] Failed to load Sponza model!");
        return;
    }
    m_Model = std::move(models[0]);

    LOG_INFO("[SponzaTest] Scene loaded: {} sub-meshes", m_Model->GetSubMeshes().size());
}

void Sponza::Run() {
    auto &platform = m_Engine.GetPlatform();

    while (m_IsRunning) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - m_LastFrameTime).count();
        m_LastFrameTime = now;
        if (dt > 0.1f) dt = 0.1f;

        m_AccumulatedTime += dt;
        m_FrameCount++;

        if (!platform.PollEvents(&m_InputManager))
            m_IsRunning = false;

        // Toggle mouse capture
        bool ctrlPressed = m_InputManager.IsKeyDown(Manro::Key::LeftCtrl);
        if (ctrlPressed && !m_CtrlPressedLastFrame) {
            m_InputCaptured = !m_InputCaptured;
            auto *window = platform.GetWindowManager().Get(m_Window);
            if (window) {
                window->CaptureMouse(m_InputCaptured);
                window->ShowCursor(!m_InputCaptured);
            }
        }
        m_CtrlPressedLastFrame = ctrlPressed;

        if (m_InputManager.IsKeyDown(Manro::Key::Escape))
            m_IsRunning = false;

        if (m_InputCaptured) {
            m_Camera.Update(m_InputManager, dt);
        } else {
            m_InputManager.ConsumeMouseDelta();
        }
        Render(dt);
    }
}

void Sponza::Render(float dt) {
    if (!m_Renderer) return;

    if (!m_Renderer->BeginFrame())
        return;
    const float aspect = m_Renderer->GetAspectRatio();
    m_Renderer->SetViewProjection(m_Camera.View(), m_Camera.Projection(kFov, aspect, kNearZ, kFarZ));
 
    m_Renderer->ClearLights();
    // Manro::LightData sun{};
    // sun.type       = shaderio::eLightTypeDirectional;
    // sun.direction  = {-0.8f, -0.6f, -0.2f};
    // sun.color      = {1.0f, 0.98f, 0.95f};
    // sun.intensity  = 6.0f;
    // m_Renderer->AddLight(sun);

    Manro::LightData fill{};
    fill.type      = shaderio::eLightTypeDirectional;
    fill.direction = {0.5f, -0.7f, 0.5f};
    fill.color     = {0.5f, 0.6f, 0.8f};
    fill.intensity = 1.5f;
    m_Renderer->AddLight(fill);

    if (m_Model) {
        m_Renderer->DrawModel(*m_Model, Manro::Mat4{1.0f});
    }

    // for (int i = 0; i < 60; ++i) {
    //     for (int j = 0; j < 2; ++j) {
    //         float x = (j == 0) ? -600.f : 600.f;
    //         float z = -1500.f + i * 50.f;
    //         float y = 200.f + sinf(m_AccumulatedTime * 2.0f + i * 0.5f) * 100.f;
    //
    //         Manro::LightData p{};
    //         p.type = shaderio::eLightTypePoint;
    //         p.position = {x, y, z};
    //         if (i % 3 == 0)      p.color = {1.0f, 0.2f, 0.2f}; // Red
    //         else if (i % 3 == 1) p.color = {0.2f, 1.0f, 0.2f}; // Green
    //         else                 p.color = {0.2f, 0.2f, 1.0f}; // Blue
    //
    //         p.intensity = 500.f;
    //         p.angularSizeOrInvRange = 1.0f / 300.0f;
    //         m_Renderer->AddLight(p);
    //     }
    // }

    m_Renderer->BeginRendering({0.02f, 0.02f, 0.05f, 1.f});
    m_Renderer->RenderQueue();
    m_Renderer->EndRendering();
    m_Renderer->EndFrameAndPresent();
}

void Sponza::Shutdown() {
    m_Model.reset();
    m_Renderer.reset();
    m_IsRunning = false;
}
