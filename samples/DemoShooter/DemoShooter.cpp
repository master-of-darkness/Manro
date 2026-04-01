#include "DemoShooter.h"
#include <Manro/Render/DebugDraw.h>
#include <Manro/Resource/Primitives.h>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

static float kFov = 90.f;
static constexpr float kNearZ = 1.f;
static constexpr float kFarZ = 50000.f;
static constexpr Manro::u32 kWindowWidth = 1920;
static constexpr Manro::u32 kWindowHeight = 1080;

static constexpr float kTPSDistance = 250.f;
static constexpr float kEyeOffset = 150.f;
static constexpr float kTPSPivotOffset = 100.f;

Manro::WindowDesc DemoShooter::GetWindowDesc() const {
    Manro::WindowDesc d;
    d.Title = "Demo Shooter";
    d.Width = kWindowWidth;
    d.Height = kWindowHeight;
    d.Fullscreen = false;
    d.Resizable = true;
    return d;
}

void DemoShooter::OnStartup(const Manro::InitContext &ctx) {
    m_Window = &ctx.Window;
    m_Jobs = &ctx.Jobs;
    m_Renderer = &ctx.Renderer;

    Manro::VirtualFS::Get().SetBaseDir(MANRO_ASSETS_DIR);
    m_InputManager.SetBackend(&m_InputBackend);
    m_Yaw = 0.f;

    m_PhysicsWorld = Manro::CreateScope<Manro::PhysicsWorld>();

    m_CameraPosition = Manro::Vec3(-1500.f, 500.f, 0.f);

    m_PlayerBody = m_PhysicsWorld->AddDynamicCapsule(m_CameraPosition, 30.f, 60.f);

    LoadAssets();

    LOG_INFO(
            "[DemoShooter] Ready. WASD=walk  Mouse=look  Shift=sprint  Space=jump  F1=noclip  F2=physics  F3=third-person  Ctrl=capture  Esc=quit");
}

void DemoShooter::OnShutdown() {
    m_CubeMaterial.reset();
    m_PhysicsWorld.reset();
}

bool DemoShooter::OnUpdate(const Manro::FrameContext &ctx, const Manro::UserCmd & /*cmd*/) {
    if (!m_IsRunning) return false;

    const float dt = ctx.DeltaTime;

    const bool ctrlDown = m_InputManager.IsKeyDown(Manro::Key::LeftCtrl);
    const bool f11Down = m_InputManager.IsKeyDown(Manro::Key::F11);
    const bool f3Down = m_InputManager.IsKeyDown(Manro::Key::F3);

    if (ctrlDown && !m_CtrlWasDown) m_InputCaptured = !m_InputCaptured;
    if (f11Down && !m_F11WasDown) m_Window->ToggleFullscreen();
    if (f3Down && !m_F3WasDown) m_ThirdPerson = !m_ThirdPerson;
    m_CtrlWasDown = ctrlDown;
    m_F11WasDown = f11Down;
    m_F3WasDown = f3Down;

    if (m_InputManager.IsKeyDown(Manro::Key::Escape)) return false;

    const bool vDown = m_InputManager.IsKeyDown(Manro::Key::F1);
    const bool f2Down = m_InputManager.IsKeyDown(Manro::Key::F2);
    if (vDown && !m_VWasDown) {
        m_NoClip = !m_NoClip;
        m_PhysicsWorld->SetLinearVelocity(m_PlayerBody, {0.f, 0.f, 0.f});
        m_PhysicsWorld->SetBodyMotionType(m_PlayerBody, m_NoClip);
        m_VerticalVelocity = 0.f;
    }
    if (f2Down && !m_F2WasDown) m_ShowPhysics = !m_ShowPhysics;
    m_VWasDown = vDown;
    m_F2WasDown = f2Down;

    if (m_InputCaptured) {
        auto [dx, dy] = m_InputManager.ConsumeMouseDelta();
        m_Yaw += dx * m_MouseSensitivity;
        m_Pitch = std::clamp(m_Pitch - dy * m_MouseSensitivity, -89.f, 89.f);

        using K = Manro::Key;
        const float yRad = glm::radians(m_Yaw);
        const float pRad = glm::radians(m_Pitch);
        const Manro::Vec3 fwd = glm::normalize(Manro::Vec3{cosf(yRad), 0.f, sinf(yRad)});
        const Manro::Vec3 right = glm::normalize(Manro::Vec3{-sinf(yRad), 0.f, cosf(yRad)});
        const Manro::Vec3 up = {0.f, 1.f, 0.f};

        if (m_NoClip) {
            const Manro::Vec3 lookDir = glm::normalize(Manro::Vec3{
                    cosf(pRad) * cosf(yRad), sinf(pRad), cosf(pRad) * sinf(yRad)});

            Manro::Vec3 moveDir{0.f};
            if (m_InputManager.IsKeyDown(K::W)) moveDir += lookDir;
            if (m_InputManager.IsKeyDown(K::S)) moveDir -= lookDir;
            if (m_InputManager.IsKeyDown(K::D)) moveDir += right;
            if (m_InputManager.IsKeyDown(K::A)) moveDir -= right;
            if (m_InputManager.IsKeyDown(K::E)) moveDir += up;
            if (m_InputManager.IsKeyDown(K::Q)) moveDir -= up;

            const Manro::Vec3 vel = (glm::length(moveDir) > 0.001f)
                                    ? glm::normalize(moveDir) * m_MoveSpeed
                                    : Manro::Vec3{0.f};
            m_PhysicsWorld->SetKinematicVelocity(m_PlayerBody, vel);
            m_VerticalVelocity = 0.f;
        } else {
            Manro::Vec3 moveDir{0.f};
            if (m_InputManager.IsKeyDown(K::W)) moveDir += fwd;
            if (m_InputManager.IsKeyDown(K::S)) moveDir -= fwd;
            if (m_InputManager.IsKeyDown(K::D)) moveDir += right;
            if (m_InputManager.IsKeyDown(K::A)) moveDir -= right;
            if (glm::length(moveDir) > 0.1f) moveDir = glm::normalize(moveDir);

            const float currentVy = m_PhysicsWorld->GetBodyLinearVelocity(m_PlayerBody).y;
            m_PhysicsWorld->SetLinearVelocity(m_PlayerBody,
                                              {moveDir.x * m_MoveSpeed, currentVy, moveDir.z * m_MoveSpeed});

            m_IsGrounded = m_PhysicsWorld->IsGrounded(m_PlayerBody);
            if (m_IsGrounded && m_InputManager.IsKeyDown(K::Space)) {
                m_PhysicsWorld->ApplyLinearImpulse(m_PlayerBody, {0.f, m_JumpForce, 0.f});
                m_IsGrounded = false;
            }

            m_VerticalVelocity = m_PhysicsWorld->GetBodyLinearVelocity(m_PlayerBody).y;
        }
    } else {
        m_InputManager.ConsumeMouseDelta();
    }

    m_PhysicsWorld->Step(dt);

    if (m_PlayerBody != Manro::kInvalidBodyHandle)
        m_CameraPosition = m_PhysicsWorld->GetBodyPosition(m_PlayerBody);

    return true;
}

void DemoShooter::OnRender(Manro::FrameContext &frame) {
    const float dt = frame.DeltaTime;

    m_Window->CaptureMouse(m_InputCaptured);
    m_Window->ShowCursor(!m_InputCaptured);

    const float pRad = glm::radians(m_Pitch);
    const float yRad = glm::radians(m_Yaw);
    const Manro::Vec3 fwd = {cosf(pRad) * cosf(yRad), sinf(pRad), cosf(pRad) * sinf(yRad)};

    Manro::Vec3 eyePos;
    if (m_ThirdPerson) {
        const Manro::Vec3 pivot = m_CameraPosition + Manro::Vec3(0.f, kTPSPivotOffset, 0.f);
        eyePos = pivot - fwd * kTPSDistance;
    } else {
        eyePos = m_CameraPosition + Manro::Vec3(0.f, kEyeOffset, 0.f);
    }

    const Manro::Mat4 view = glm::lookAt(eyePos, eyePos + fwd, {0.f, 1.f, 0.f});
    const Manro::Mat4 proj = glm::perspective(glm::radians(kFov),
                                              m_Renderer->GetAspectRatio(), kNearZ, kFarZ);

    m_Renderer->SetViewProjection(view, proj);
    m_Renderer->SetCameraPosition(eyePos);

    m_Renderer->ClearLights();
    Manro::LightData sun{};
    sun.type = shaderio::eLightTypeDirectional;
    sun.direction = glm::normalize(Manro::Vec3{0.5f, -1.0f, 0.3f});
    sun.color = {1.0f, 0.98f, 0.90f};
    sun.intensity = 3.0f;
    m_Renderer->AddLight(sun);

    m_Renderer->BeginRendering();

    if (m_ShowPhysics)
        m_PhysicsWorld->DrawDebug(*m_Renderer);

    m_Renderer->RenderQueue();
    m_Renderer->EndRendering();

    m_LastStats = m_Renderer->GetLastFrameStats();
    const float frameMs = dt * 1000.f;
    m_FrameTimeHistory[m_FrameTimeOffset] = frameMs;
    m_FrameTimeOffset = (m_FrameTimeOffset + 1) % kHistoryLen;
}

void DemoShooter::LoadAssets() {
    struct BoxDef {
        Manro::Vec3 center;
        Manro::Vec3 halfExtents;
    };

    std::vector<BoxDef> layout = {
            // Floor
            {{0.f,     -50.f, 0.f},     {4000.f, 50.f,  4000.f}},

            {{-1000.f, 100.f, -1000.f}, {200.f,  100.f, 200.f}},
            {{-1000.f, 300.f, -1000.f}, {100.f,  100.f, 100.f}},
            {{-1500.f, 200.f, -1000.f}, {50.f,   200.f, 300.f}},

            {{1000.f,  100.f, 1000.f},  {300.f,  100.f, 100.f}},
            {{1500.f,  150.f, 1000.f},  {100.f,  150.f, 100.f}},

            {{0.f,     150.f, 0.f},     {400.f,  150.f, 50.f}},
            {{-200.f,  150.f, 0.f},     {50.f,   150.f, 50.f}},
            {{200.f,   150.f, 0.f},     {50.f,   150.f, 50.f}},

            {{0.f,     50.f,  2000.f},  {500.f,  50.f,  500.f}},

            {{0.f,     50.f,  -2000.f}, {500.f,  50.f,  500.f}}
    };

    for (const auto &b: layout) {
        m_PhysicsWorld->AddStaticBox(b.center, b.halfExtents);
    }
    LOG_INFO("[DemoShooter] Physics mesh built from cubes.");

    auto skyFaces = Manro::TextureLoader::LoadCubemap("skyboxes/cubemap_sky.png");
    if (!skyFaces.empty()) {
        auto h = m_Renderer->UploadCubemap(skyFaces);
        m_Renderer->SetSkybox(h);
    }

    m_CubeMesh = m_Renderer->UploadMesh(Manro::Primitives::CreateCube(2.0f));
    m_CubeMaterial = m_Renderer->CreateMaterialInstance(m_Renderer->GetDefaultMaterial());

    for (const auto &b: layout) {
        m_Blocks.push_back({b.center, b.halfExtents});

        glm::mat4 model = glm::translate(glm::mat4(1.f), b.center);
        model = glm::scale(model, b.halfExtents);
        m_Renderer->DrawMeshStatic(m_CubeMesh, *m_CubeMaterial, model);
    }

    LOG_INFO("[DemoShooter] Assets loaded.");
}
