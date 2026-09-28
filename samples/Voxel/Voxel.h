#pragma once

#include <Manro/Interfaces/IApplication.h>
#include <Manro/Core/World.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/InputBackend.h>
#include <Manro/Platform/Window/Window.h>
#include <Manro/Render/Renderer.h>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cstdio>

// CVoxel: minimal validation app for the separate GPU-driven voxel path.
// - VoxelInit() builds sparse world + task/mesh PSO + HiZ + GI compute.
// - One 16^3 brick of checkerboard voxels, uploaded once (no re-mesh).
// - Per-frame: view/proj + camera -> CRenderer, Record happens inside
//   BeginRendering/RenderQueue/EndRendering via the SceneRenderer hook.
// - Space queues a GPU-local SphereEdit (no CPU re-bake).
class CVoxel final : public Manro::IApplication {
public:
    Manro::WindowDesc_t GetWindowDesc() const override {
        Manro::WindowDesc_t d;
        d.Title = "Voxel";
        d.Width = 1280;
        d.Height = 720;
        d.Fullscreen = false;
        d.Resizable = true;
        return d;
    }

    void OnStartup(const Manro::InitContext_t &ctx) override {
        m_Renderer = &ctx.CRenderer;
        m_Window = &ctx.CWindow;
        m_InputManager.SetBackend(&m_InputBackend);
        m_Renderer->SetDebugUIEnabled(false);

        // Voxel task/mesh PSO is MSAA-free (1X); force single-sample so the
        // shared color+depth pass matches rasterizationSamples.
        {
            Manro::RenderSettings_t s = m_Renderer->GetSettings();
            s.aaMode = Manro::AntiAliasingMode::None;
            s.msaaSamples = Manro::MSAASampleCount::MSAA_1X;
            m_Renderer->SetSettings(s);
        }

        // Voxel path first: sparse world + PSOs.
        m_Renderer->VoxelInit(1280, 720);

        // One brick at slot (0,0,0): solid checkerboard floor (y < 8).
        // Brick origin = world (0,0,0); camera parked 24 up / 40 out.
        m_Renderer->VoxelAllocateBrick(0, 0, 0);
        static Manro::u16 mats[4096];
        static Manro::u32 occ[128] = {};
        for (Manro::u32 z = 0; z < 16; ++z)
            for (Manro::u32 y = 0; y < 16; ++y)
                for (Manro::u32 x = 0; x < 16; ++x) {
                    const Manro::u32 i = x + y * 16 + z * 256;
                    if (y < 8) {
                        occ[i >> 5u] |= (1u << (i & 31u));
                        mats[i] = static_cast<Manro::u16>((x + z) & 1u);
                    }
                }
        // Brick index 0 = first allocation.
        m_Renderer->VoxelUploadBrick(0, mats, occ);

        m_CamPos = Manro::Vec3(8.f, 24.f, 40.f);
        // Look at brick center (8,4,8) from the start.
        m_Fwd = glm::normalize(Manro::Vec3(8.f, 4.f, 8.f) - m_CamPos);
        m_Yaw = glm::degrees(atan2f(m_Fwd.z, m_Fwd.x));
        m_Pitch = glm::degrees(asinf(std::clamp(m_Fwd.y, -1.f, 1.f)));
    }

    void OnShutdown() override {
        if (m_Renderer)
            m_Renderer->VoxelShutdown();
    }

    bool OnUpdate(const Manro::FrameContext_t &ctx, const Manro::UserCmd_t &) override {
        using K = Manro::Key;
        auto [mx, my] = m_InputManager.ConsumeMouseDelta();
        m_Yaw += mx * 0.1f;
        m_Pitch = std::clamp(m_Pitch - my * 0.1f, -89.f, 89.f);

        const float yr = glm::radians(m_Yaw), pr = glm::radians(m_Pitch);
        Manro::Vec3 fwd{cosf(pr) * cosf(yr), sinf(pr), cosf(pr) * sinf(yr)};
        fwd = glm::normalize(fwd);
        Manro::Vec3 right = glm::normalize(glm::cross(fwd, Manro::Vec3{0, 1, 0}));
        const float speed = m_InputManager.IsKeyDown(K::LeftShift) ? 60.f : 20.f;
        if (m_InputManager.IsKeyDown(K::W)) m_CamPos += fwd * speed * ctx.DeltaTime;
        if (m_InputManager.IsKeyDown(K::S)) m_CamPos -= fwd * speed * ctx.DeltaTime;
        if (m_InputManager.IsKeyDown(K::D)) m_CamPos += right * speed * ctx.DeltaTime;
        if (m_InputManager.IsKeyDown(K::A)) m_CamPos -= right * speed * ctx.DeltaTime;
        if (m_InputManager.IsKeyDown(K::Escape)) return false;

        // GPU-local edit validation: Space erases a sphere, no CPU re-mesh.
        if (m_InputManager.IsKeyDown(K::Space) && !m_bSpaceHeld) {
            m_bSpaceHeld = true;
            const Manro::Vec3 target = m_CamPos + fwd * 30.f;
            m_Renderer->VoxelQueueEdit(target, 4.f, 0, 0);
            ++m_EditCount;
        } else if (!m_InputManager.IsKeyDown(K::Space)) {
            m_bSpaceHeld = false;
        }
        m_Fwd = fwd;
        return true;
    }

    void OnRender(Manro::FrameContext_t &frame) override {
        const Manro::Mat4 view = glm::lookAt(m_CamPos, m_CamPos + m_Fwd, Manro::Vec3{0, 1, 0});
        const Manro::Mat4 proj =
            glm::perspective(glm::radians(90.f), m_Renderer->GetAspectRatio(), 0.1f, 10000.f);
        m_Renderer->SetViewProjection(view, proj);
        m_Renderer->SetCameraPosition(m_CamPos);

        m_Renderer->BeginRendering();
        m_Renderer->RenderQueue();
        m_Renderer->EndRendering();

        // On-screen FPS: EMA of frame dt + 1%/0.1% lows over a rolling window.
        const float dtMs = frame.DeltaTime * 1000.f;
        m_FpsEma = (m_FpsEma <= 0.f) ? dtMs : m_FpsEma + (dtMs - m_FpsEma) * 0.05f;
        m_FrameTimes[m_FrameTimeIdx] = dtMs;
        m_FrameTimeIdx = (m_FrameTimeIdx + 1) % kFrameTimeWindow;
        if (m_FramesSeen < kFrameTimeWindow) ++m_FramesSeen;

        Manro::u32 dbg[6] = {};
        m_Renderer->VoxelGetDebugCounters(dbg);

        float worst1 = 0.f, worst01 = 0.f;
        if (m_FramesSeen > 0) {
            float sorted[kFrameTimeWindow];
            for (Manro::u32 i = 0; i < m_FramesSeen; ++i) sorted[i] = m_FrameTimes[i];
            std::sort(sorted, sorted + m_FramesSeen);
            worst1 = sorted[(m_FramesSeen * 99) / 100];
            worst01 = sorted[(m_FramesSeen * 999) / 1000];
        }
        const float emaFps = (m_FpsEma > 0.f) ? 1000.f / m_FpsEma : 0.f;

        ImGui::SetNextWindowPos({10.f, 10.f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("Voxel HUD", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav)) {
            ImGui::Text("FPS: %.0f (%.2f ms)", static_cast<double>(emaFps),
                        static_cast<double>(m_FpsEma));
            ImGui::Text("1%% low: %.2f ms  0.1%% low: %.2f ms", static_cast<double>(worst1),
                        static_cast<double>(worst01));
            ImGui::Separator();
            ImGui::Text("Bricks: %u  TaskGroups: %u  Edits: %u",
                        m_Renderer->VoxelGetBrickCount(), m_Renderer->VoxelGetTaskGroups(),
                        m_EditCount);
            ImGui::TextDisabled("task=%u vis=%u faces=%u cull=%u mesh=%u mfaces=%u", dbg[0], dbg[1],
                                dbg[2], dbg[3], dbg[4], dbg[5]);
            ImGui::TextDisabled("WASD move | Shift fast | Space edit | Esc quit");
        }
        ImGui::End();

        const float fps = frame.DeltaTime > 0.f ? 1.f / frame.DeltaTime : 0.f;
        if ((m_Frame++ % 120) == 0) {
            printf("[Voxel] fps=%.1f bricks=%u taskGroups=%u edits=%u cam=(%.1f,%.1f,%.1f)\n", fps,
                   m_Renderer->VoxelGetBrickCount(), m_Renderer->VoxelGetTaskGroups(), m_EditCount,
                   m_CamPos.x, m_CamPos.y, m_CamPos.z);
            // DEBUG: [0]=taskRuns [1]=visible [2]=faces [3]=culled [4]=meshRuns [5]=meshFaces
            printf("[VoxelDbg] taskRuns=%u visible=%u faces=%u culled=%u meshRuns=%u meshFaces=%u\n",
                   dbg[0], dbg[1], dbg[2], dbg[3], dbg[4], dbg[5]);
        }
    }

    Manro::CInputManager *GetInputManager() override { return &m_InputManager; }

private:
    Manro::CRenderer *m_Renderer{nullptr};
    Manro::CWindow *m_Window{nullptr};
    Manro::CInputBackend m_InputBackend;
    Manro::CInputManager m_InputManager;
    Manro::Vec3 m_CamPos{0.f};
    Manro::Vec3 m_Fwd{0, 0, -1};
    float m_Yaw{-90.f};
    float m_Pitch{-10.f};
    Manro::u32 m_Frame{0};
    Manro::u32 m_EditCount{0};
    bool m_bSpaceHeld{false};

    // On-screen FPS state: EMA of frame dt + rolling window for lows.
    static constexpr Manro::u32 kFrameTimeWindow = 240;
    float m_FrameTimes[kFrameTimeWindow]{};
    Manro::u32 m_FrameTimeIdx{0};
    Manro::u32 m_FramesSeen{0};
    float m_FpsEma{0.f};
};
