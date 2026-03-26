#pragma once

#include <Manro/Core/IApplication.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Render/Model.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <chrono>
#include <vector>
#include "Manro/Render/Renderer.h"

namespace Manro {
    class Renderer;
    class JobSystem;
}

struct FlyCamera {
    Manro::Vec3 Position{0.f, 150.f, 0.f};
    float       Yaw{-90.f};
    float       Pitch{-10.f};
    float       NormalSpeed{200.f};
    float       SprintSpeed{800.f};
    float       MouseSensitivity{0.1f};

    Manro::Vec3 Forward() const;
    void        Update(const Manro::InputManager& input, float dt);
    Manro::Mat4 View() const;

    static Manro::Mat4 Projection(float fovDeg, float aspect, float nearZ, float farZ);
};

struct BenchWaypoint {
    Manro::Vec3 position;
    float       yaw;    // degrees
    float       pitch;  // degrees
};

enum class BenchmarkState { Idle, Warmup, Running, Done };

struct BenchmarkResult {
    float        avgFps{0.f};
    float        minFps{0.f};
    float        maxFps{0.f};
    float        avgFrameMs{0.f};
    float        p1FrameMs{0.f};
    float        p01FrameMs{0.f};
    float        totalSeconds{0.f};
    Manro::u32   totalFrames{0};
    Manro::u32   avgDrawCalls{0};
    Manro::u32   avgTriangles{0};
};

class Sponza final : public Manro::IApplication {
public:
    Sponza()  = default;
    ~Sponza() = default;

    Manro::WindowDesc GetWindowDesc() const override;
    void OnStartup()  override;
    void OnShutdown() override;
    bool OnUpdate(const Manro::FrameContext& ctx, const Manro::UserCmd& cmd) override;
    void OnRender(Manro::RenderContext& ctx) override;
    Manro::InputManager* GetInputManager() override { return &m_InputManager; }

private:
    void LoadScene(Manro::Renderer& renderer, Manro::JobSystem& jobs);

    void DrawGui(Manro::Renderer& renderer, float dt);

    void StartBenchmark();
    void TickBenchmark(Manro::Renderer& renderer, float dt);
    void FinishBenchmark(Manro::Renderer& renderer);
    void AdvanceBenchCamera(float dt);

    static Manro::Vec3 CatmullRomPos(const BenchWaypoint& p0, const BenchWaypoint& p1,
                                     const BenchWaypoint& p2, const BenchWaypoint& p3,
                                     float t);
    static float CatmullRomAngle(float a0, float a1, float a2, float a3, float t);

    Manro::JobSystem       m_LoadJobs;
    Manro::SDL3InputBackend m_InputBackend;
    Manro::InputManager     m_InputManager;

    Manro::Scope<Manro::Model> m_Model;

    FlyCamera m_Camera;
    bool      m_InputCaptured{true};
    bool      m_CtrlWasDown{false};
    bool      m_F11WasDown{false};

    float m_AccumulatedTime{0.f};

    Manro::FrameStats m_LastStats{};
    static constexpr int kHistoryLen = 120;
    float m_FrameTimeHistory[kHistoryLen]{};
    int   m_FrameTimeOffset{0};

    BenchmarkState  m_BenchState{BenchmarkState::Idle};
    BenchmarkResult m_BenchResult{};
    float           m_BenchElapsed{0.f};
    float           m_WarmupElapsed{0.f};
    int             m_BenchDuration{30};
    float           m_WarmupDuration{3.f};
    int             m_BenchLightCount{8};
    Manro::u64      m_BenchDrawCallsAcc{0};
    Manro::u64      m_BenchTrianglesAcc{0};
    std::vector<float>            m_BenchFrameTimes;
    std::vector<Manro::LightData> m_BenchLights;

    float m_PathT{0.f};
    float m_PathSpeed{0.25f};

    Manro::Vec3 m_SavedCamPos{};
    float       m_SavedCamYaw{0.f};
    float       m_SavedCamPitch{0.f};

    bool m_ShowBenchWindow{false};
    bool m_IsRunning{true};

    static const BenchWaypoint kWaypoints[];
    static const int           kWaypointCount;
};