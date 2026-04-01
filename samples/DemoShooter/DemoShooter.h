#pragma once

#include <Manro/Interfaces/IApplication.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Render/Model.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Physics/PhysicsWorld.h>

namespace Manro {
    class Renderer;

    class JobSystem;
}

class DemoShooter final : public Manro::IApplication {
public:
    DemoShooter() = default;

    ~DemoShooter() = default;

    Manro::WindowDesc GetWindowDesc() const override;

    void OnStartup(const Manro::InitContext &ctx) override;

    void OnShutdown() override;

    bool OnUpdate(const Manro::FrameContext &ctx, const Manro::UserCmd &cmd) override;

    void OnRender(Manro::FrameContext &frame) override;

    Manro::InputManager *GetInputManager() override { return &m_InputManager; }

private:
    void LoadAssets();

    Manro::IWindow *m_Window{nullptr};
    Manro::JobSystem *m_Jobs{nullptr};
    Manro::Renderer *m_Renderer{nullptr};

    Manro::SDL3InputBackend m_InputBackend;
    Manro::InputManager m_InputManager;

    Manro::Scope<Manro::PhysicsWorld> m_PhysicsWorld;
    Manro::PhysicsBodyHandle m_PlayerBody{Manro::kInvalidBodyHandle};

    Manro::Vec3 m_CameraPosition{0.f, 200.f, 500.f};
    float m_Yaw{-90.f};
    float m_Pitch{0.f};
    float m_MouseSensitivity{0.15f};
    float m_MoveSpeed{500.f};
    float m_JumpForce{30.f};
    float m_VerticalVelocity{0.f};
    bool m_IsGrounded{false};

    bool m_NoClip{false};
    bool m_InputCaptured{true};
    bool m_CtrlWasDown{false};
    bool m_F11WasDown{false};
    bool m_ShowPhysics{true};
    bool m_F2WasDown{false};
    bool m_F3WasDown{false};
    bool m_VWasDown{false};
    bool m_ThirdPerson{false};

    Manro::FrameStats m_LastStats{};
    static constexpr int kHistoryLen = 120;
    float m_FrameTimeHistory[kHistoryLen]{};
    int m_FrameTimeOffset{0};

    bool m_IsRunning{true};

    struct BlockInst {
        Manro::Vec3 center;
        Manro::Vec3 halfExtents;
    };
    std::vector<BlockInst> m_Blocks;
    Manro::MeshHandle m_CubeMesh;
    Manro::Scope<Manro::MaterialInstance> m_CubeMaterial;
};
