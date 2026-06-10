#pragma once

#include <Manro/Interfaces/IApplication.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Physics/PhysicsWorld.h>
#include <Manro/Render/Model.h>
#include <Manro/Resource/Map.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/InputBackend.h>
#include <chrono>
#include <unordered_map>
#include <vector>
#include "Manro/Render/Renderer.h"

namespace Manro {
    class CRenderer;

    class CJobSystem;

    class CVirtualFS;
}

struct FlyCamera_t {
    Manro::Vec3 Position{0.f, 150.f, 0.f};
    float Yaw{-90.f};
    float Pitch{-10.f};
    float NormalSpeed{200.f};
    float SprintSpeed{800.f};
    float MouseSensitivity{0.1f};

    Manro::Vec3 Forward() const;

    void Update(const Manro::CInputManager &input, float dt);

    Manro::Mat4 View() const;

    static Manro::Mat4 Projection(float fovDeg, float aspect, float nearZ, float farZ);
};

struct BenchWaypoint_t {
    Manro::Vec3 position;
    float yaw; // degrees
    float pitch; // degrees
};

enum class BenchmarkState {
    Idle, Warmup, Running, Done
};

struct BenchmarkResult_t {
    float avgFps{0.f};
    float minFps{0.f};
    float maxFps{0.f};
    float avgFrameMs{0.f};
    float p1FrameMs{0.f};
    float p01FrameMs{0.f};
    float totalSeconds{0.f};
    Manro::u32 totalFrames{0};
    Manro::u32 avgDrawCalls{0};
    Manro::u32 avgTriangles{0};
};

class CSponza final : public Manro::IApplication {
public:
    CSponza() = default;

    ~CSponza() = default;

    Manro::WindowDesc_t GetWindowDesc() const override;

    void OnStartup(const Manro::InitContext_t &ctx) override;

    void OnShutdown() override;

    bool OnUpdate(const Manro::FrameContext_t &ctx, const Manro::UserCmd_t &cmd) override;

    void OnRender(Manro::FrameContext_t &frame) override;

    Manro::CInputManager *GetInputManager() override { return &m_InputManager; }

private:
    void LoadScene();

    void BuildWorldColliders();

    void StepPlayer(float dt);

    void DrawGui(float dt);

    void StartBenchmark();

    void TickBenchmark(float dt);

    void FinishBenchmark();

    void AdvanceBenchCamera(float dt);

    static Manro::Vec3 CatmullRomPos(const BenchWaypoint_t &p0, const BenchWaypoint_t &p1,
                                     const BenchWaypoint_t &p2, const BenchWaypoint_t &p3,
                                     float t);

    static float CatmullRomAngle(float a0, float a1, float a2, float a3, float t);

    Manro::CWindow *m_Window{nullptr};
    Manro::CJobSystem *m_Jobs{nullptr};
    Manro::CRenderer *m_Renderer{nullptr};
    Manro::CVirtualFS *m_Vfs{nullptr};

    Manro::CJobSystem m_LoadJobs;
    Manro::CInputBackend m_InputBackend;
    Manro::CInputManager m_InputManager;

    Manro::CMap m_Map;
    std::unordered_map<std::string, Manro::Scope<Manro::CModel>> m_MapModels;

    Manro::Scope<Manro::CPhysicsWorld> m_Physics;
    Manro::PhysicsBodyHandle m_PlayerBody{Manro::kInvalidBodyHandle};
    std::vector<Manro::PhysicsBodyHandle> m_StaticWorldBodies;
    Manro::Vec3 m_PlayerVelocity{0.f};
    static constexpr Manro::Vec3 kPlayerHalfExtents{20.f, 45.f, 20.f};
    static constexpr float kEyeOffsetY = 30.f;
    static constexpr float kJumpSpeed = 350.f;
    static constexpr float kGravity = 980.f;
    static constexpr float kWalkSpeed = 250.f;
    static constexpr float kRunSpeed = 500.f;

    FlyCamera_t m_Camera;
    bool m_bInputCaptured{true};
    bool m_bCtrlWasDown{false};
    bool m_bF1WasDown{false};
    bool m_bF11WasDown{false};

    float m_flAccumulatedTime{0.f};

    Manro::FrameStats_t m_LastStats{};
    static constexpr int kHistoryLen = 120;
    float m_flFrameTimeHistory[kHistoryLen]{};
    int m_nFrameTimeOffset{0};

    BenchmarkState m_BenchState{BenchmarkState::Idle};
    BenchmarkResult_t m_BenchResult{};
    float m_flBenchElapsed{0.f};
    float m_flWarmupElapsed{0.f};
    int m_nBenchDuration{30};
    float m_flWarmupDuration{3.f};
    int m_nBenchLightCount{8};
    Manro::u64 m_BenchDrawCallsAcc{0};
    Manro::u64 m_BenchTrianglesAcc{0};
    std::vector<float> m_BenchFrameTimes;
    std::vector<Manro::LightData> m_BenchLights;

    float m_flPathT{0.f};
    float m_flPathSpeed{0.25f};

    Manro::Vec3 m_SavedCamPos{};
    float m_flSavedCamYaw{0.f};
    float m_flSavedCamPitch{0.f};

    bool m_bShowBenchWindow{false};
    bool m_bIsRunning{true};

    static const BenchWaypoint_t kWaypoints[];
    static const int kWaypointCount;
};
