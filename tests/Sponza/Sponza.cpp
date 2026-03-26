#include "Sponza.h"

#include <Manro/Core/IApplication.h>
#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Render/Gui/ImGuiLayer.h>
#include <Manro/Platform/Window/IWindow.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>

static constexpr auto  kSponzaPath   = "models/sponza/Sponza.gltf";
static float           kFov          = 100.f;
static constexpr float kNearZ        = 1.f;
static constexpr float kFarZ         = 10000.f;
static constexpr Manro::u32 kWindowWidth  = 1920;
static constexpr Manro::u32 kWindowHeight = 1080;

static float s_TimeOfDay  = 10.0f;
static bool  s_AnimateSun = true;
static float s_DaySpeed   = 0.5f;

Manro::WindowDesc Sponza::GetWindowDesc() const {
    Manro::WindowDesc d;
    d.Title      = "Sponza Test";
    d.Width      = kWindowWidth;
    d.Height     = kWindowHeight;
    d.Fullscreen = false;
    d.Resizable  = true;
    return d;
}

void Sponza::OnStartup(const Manro::InitContext &ctx) {
    m_Window = &ctx.Window;
    m_Jobs = &ctx.Jobs;
    m_Renderer = &ctx.Renderer;

    Manro::VirtualFS::Get().SetBaseDir(MANRO_ASSETS_DIR);
    m_InputManager.SetBackend(&m_InputBackend);
    m_BenchFrameTimes.reserve(30 * 500);

    LoadScene(*m_Renderer, *m_Jobs);

    LOG_INFO("[SponzaTest] Ready. WASD=move Mouse=look Shift=sprint Q/E=up/down Escape=quit");
}

void Sponza::OnShutdown() {
    m_Model.reset();
}

bool Sponza::OnUpdate(const Manro::FrameContext& ctx, const Manro::UserCmd& /*cmd*/) {
    if (!m_IsRunning) return false;

    const float dt = ctx.DeltaTime;
    m_AccumulatedTime += dt;

    if (s_AnimateSun) {
        s_TimeOfDay += dt * s_DaySpeed;
        if (s_TimeOfDay >= 24.f) s_TimeOfDay -= 24.f;
    }

    const bool ctrlDown = m_InputManager.IsKeyDown(Manro::Key::LeftCtrl);
    const bool f11Down  = m_InputManager.IsKeyDown(Manro::Key::F11);

    if (ctrlDown && !m_CtrlWasDown) m_InputCaptured = !m_InputCaptured;
    if (f11Down && !m_F11WasDown) {
        m_Window->ToggleFullscreen();
        m_F11WasDown = !m_F11WasDown;
    }
    m_CtrlWasDown = ctrlDown;
    m_F11WasDown  = f11Down;

    if (m_InputManager.IsKeyDown(Manro::Key::Escape)) {
        if (m_BenchState == BenchmarkState::Running ||
            m_BenchState == BenchmarkState::Warmup) {
            m_BenchState = BenchmarkState::Idle;
            m_BenchFrameTimes.clear();
            m_Camera.Position = m_SavedCamPos;
            m_Camera.Yaw      = m_SavedCamYaw;
            m_Camera.Pitch    = m_SavedCamPitch;
            m_InputCaptured   = true;
        } else {
            return false;
        }
    }

    const bool benchActive = (m_BenchState == BenchmarkState::Warmup ||
                              m_BenchState == BenchmarkState::Running);
    if (benchActive) {
        AdvanceBenchCamera(dt);
    } else if (m_InputCaptured) {
        m_Camera.Update(m_InputManager, dt);
    } else {
        m_InputManager.ConsumeMouseDelta();
    }

    return true;
}

void Sponza::OnRender(Manro::RenderContext& ctx) {
    Manro::Renderer& renderer = ctx.Renderer;
    const float dt = ctx.Frame.DeltaTime;

    m_Window->CaptureMouse(m_InputCaptured);
    m_Window->ShowCursor(!m_InputCaptured);

    if (!m_Model) LoadScene(renderer, *m_Jobs);

    renderer.SetViewProjection(
            m_Camera.View(),
            FlyCamera::Projection(kFov, renderer.GetAspectRatio(), kNearZ, kFarZ));
    renderer.SetCameraPosition(m_Camera.Position);

    renderer.ClearLights();

    const float dayTau     = (s_TimeOfDay / 24.f) * 2.f * 3.14159265f;
    const float sunAlt     = sinf(dayTau - 1.5707f);
    const float sunAzimuth = cosf(dayTau - 1.5707f);

    Manro::LightData sun{};
    sun.type      = shaderio::eLightTypeDirectional;
    sun.direction = glm::normalize(Manro::Vec3{sunAzimuth, -sunAlt, 0.3f});

    if (sunAlt > 0.05f) {
        sun.color     = {1.0f, 0.98f, 0.90f};
        sun.intensity = 3.f * sunAlt;
    } else if (sunAlt > -0.1f) {
        const float t = (sunAlt + 0.1f) / 0.15f;
        sun.color     = glm::mix(Manro::Vec3{1.f, 0.3f, 0.05f},
                                 Manro::Vec3{1.f, 0.98f, 0.90f}, t);
        sun.intensity = 0.8f;
    } else {
        sun.direction = glm::normalize(Manro::Vec3{-sunAzimuth, sunAlt, -0.3f});
        sun.color     = {0.1f, 0.15f, 0.35f};
        sun.intensity = 0.2f;
    }
    renderer.AddLight(sun);

    const bool benchActive = (m_BenchState == BenchmarkState::Warmup ||
                              m_BenchState == BenchmarkState::Running);
    if (benchActive) {
        for (const auto& l : m_BenchLights) renderer.AddLight(l);
        TickBenchmark(renderer, dt);
    }

    Manro::Vec4 clearColor{0.02f, 0.02f, 0.05f, 1.f};
    if (sunAlt > 0.f)
        clearColor = glm::mix(Manro::Vec4{0.1f, 0.05f, 0.02f, 1.f},
                              Manro::Vec4{0.4f, 0.6f, 0.9f,  1.f}, sunAlt);

    renderer.BeginRendering(clearColor);
    renderer.RenderQueue();
    renderer.EndRendering();

    DrawGui(renderer, dt);

    m_LastStats = renderer.GetLastFrameStats();
    const float frameMs = dt * 1000.f;
    m_FrameTimeHistory[m_FrameTimeOffset] = frameMs;
    m_FrameTimeOffset = (m_FrameTimeOffset + 1) % kHistoryLen;
}

void Sponza::LoadScene(Manro::Renderer& renderer, Manro::JobSystem& jobs) {
    auto models = Manro::Model::Load({kSponzaPath}, renderer, jobs);

    if (models.empty() || !models[0]) {
        LOG_ERROR("[SponzaTest] Failed to load Sponza!");
        return;
    }
    m_Model = std::move(models[0]);
    renderer.DrawModelStatic(*m_Model,
                             glm::scale(glm::mat4(1.f), glm::vec3(100.f)));
    LOG_INFO("[SponzaTest] Sponza loaded.");
}

void Sponza::DrawGui(Manro::Renderer& renderer, const float dt) {
    const float fps   = dt > 0.f ? 1.f / dt : 0.f;
    const float msdt  = dt * 1000.f;
    const bool  benchActive = (m_BenchState == BenchmarkState::Warmup ||
                               m_BenchState == BenchmarkState::Running);

    ImGui::SetNextWindowPos({10, 10},  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({320, 0}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);

    if (ImGui::Begin("Manro Profiler")) {

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("FPS        %.1f", fps);
            ImGui::Text("Frame      %.3f ms", msdt);
            float tmp[kHistoryLen];
            for (int i = 0; i < kHistoryLen; ++i)
                tmp[i] = m_FrameTimeHistory[(m_FrameTimeOffset + i) % kHistoryLen];
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%.2f ms", msdt);
            ImGui::PlotLines("##ft", tmp, kHistoryLen, 0, overlay, 0.f, 33.3f, {0, 60});
        }

        if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Draw calls  %u", m_LastStats.drawCalls);
            ImGui::Text("Triangles   %u", m_LastStats.triangleCount);
            ImGui::Text("Instances   %u", m_LastStats.instanceCount);
            ImGui::Text("Lights      %u", m_LastStats.lightCount);
        }

        if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            Manro::RenderSettings settings = renderer.GetSettings();
            bool changed = false;

            if (ImGui::SliderFloat("Resolution Scale", &settings.resolutionScale, 0.1f, 2.0f)) changed = true;
            if (ImGui::Checkbox("VSync",              &settings.enableVSync))           changed = true;
            if (ImGui::Checkbox("Frustum Culling",    &settings.enableFrustumCulling))  changed = true;

            if (ImGui::TreeNode("Camera")) {
                if (ImGui::DragFloat("Near Z", &settings.nearZ, 0.01f, 0.001f, 10.f))       changed = true;
                if (ImGui::DragFloat("Far Z",  &settings.farZ,  10.f,  100.f,  100000.f))   changed = true;
                if (ImGui::SliderFloat("FoV",  &kFov,           50.f,  120.f))              {}
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Environment & Sun")) {
                ImGui::Checkbox("Animate Day Cycle", &s_AnimateSun);
                ImGui::SliderFloat("Time",      &s_TimeOfDay, 0.f, 24.f, "%.1f h");
                ImGui::SliderFloat("Day Speed", &s_DaySpeed,  0.f,  5.f);
                ImGui::Separator();
                if (ImGui::SliderFloat("IBL Intensity", &settings.lighting.iblIntensity, 0.f, 5.f))  changed = true;
                if (ImGui::SliderFloat("Gamma",          &settings.lighting.gamma,        1.f, 3.f))  changed = true;
                if (ImGui::Checkbox("AO Enabled", &settings.lighting.enableAmbientOcclusion))         changed = true;
                if (settings.lighting.enableAmbientOcclusion) {
                    if (ImGui::SliderFloat("AO Intensity", &settings.lighting.aoIntensity, 0.f, 2.f)) changed = true;
                    if (ImGui::SliderFloat("AO Radius",    &settings.lighting.aoRadius,    0.01f, 2.f))changed = true;
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Shadows")) {
                if (ImGui::Checkbox("Enabled##Shadows",  &settings.shadows.enabled))                    changed = true;
                if (ImGui::DragInt("Resolution",          &settings.shadows.resolution, 128, 128, 4096)) changed = true;
                if (ImGui::SliderFloat("Bias",            &settings.shadows.bias,       0.f, 0.05f, "%.4f")) changed = true;
                if (ImGui::SliderFloat("Slope Bias",      &settings.shadows.slopeBias,  0.f, 0.5f))          changed = true;
                if (ImGui::SliderFloat("Softness",        &settings.shadows.softShadows, 0.f, 5.f))          changed = true;
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Post-Processing")) {
                auto& tm = settings.postProcess.tonemapping;
                if (ImGui::SliderFloat("Exposure",   &tm.exposure,    0.f, 10.f)) changed = true;
                if (ImGui::SliderFloat("Contrast",   &tm.contrast,    0.f,  3.f)) changed = true;
                if (ImGui::SliderFloat("Saturation", &tm.saturation,  0.f,  3.f)) changed = true;
                const char* methods[] = {"Filmic","Uncharted2","Clip","ACES","AgX","KhronosPBR"};
                if (ImGui::Combo("Method", &tm.method, methods, IM_ARRAYSIZE(methods))) changed = true;
                ImGui::TreePop();
            }

            if (changed) renderer.SetSettings(settings);
        }

        if (ImGui::CollapsingHeader("GPU")) {
            Manro::u64 used, budget;
            renderer.GetVramStats(used, budget);
            const float usedMB   = static_cast<float>(used)   / (1024.f * 1024.f);
            const float budgetMB = static_cast<float>(budget) / (1024.f * 1024.f);
            ImGui::Text("VRAM  %.1f / %.1f MB", usedMB, budgetMB);
            ImGui::ProgressBar(usedMB / std::max(budgetMB, 1.f), {-FLT_MIN, 0});

            ImGui::TextDisabled("%s", renderer.GetAdapterName().c_str());
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (benchActive) {
            if (m_BenchState == BenchmarkState::Warmup) {
                const float prog = m_WarmupElapsed / m_WarmupDuration;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.8f, 0.6f, 0.1f, 1.f});
                ImGui::ProgressBar(prog, {-FLT_MIN, 0}, "Warming up...");
                ImGui::PopStyleColor();
                ImGui::Text("%.1f / %.0fs", m_WarmupElapsed, m_WarmupDuration);
            } else {
                const float prog = m_BenchElapsed / static_cast<float>(m_BenchDuration);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.2f, 0.8f, 0.3f, 1.f});
                ImGui::ProgressBar(prog, {-FLT_MIN, 0}, "Benchmarking...");
                ImGui::PopStyleColor();
                ImGui::Text("%.1f / %ds  |  %u frames  |  %.1f fps",
                            m_BenchElapsed, m_BenchDuration,
                            static_cast<Manro::u32>(m_BenchFrameTimes.size()), fps);
            }
            ImGui::TextDisabled("Press Escape to cancel");
        } else {
            if (ImGui::Button("Run Benchmark", {-FLT_MIN, 0}))
                m_ShowBenchWindow = true;
            if (m_BenchState == BenchmarkState::Done)
                ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f},
                                   "%.1f avg FPS  |  %.2f ms avg",
                                   m_BenchResult.avgFps, m_BenchResult.avgFrameMs);
        }

        ImGui::Spacing();
        if (!benchActive) ImGui::TextDisabled("Left Ctrl  toggle cursor");
    }
    ImGui::End();

    if (!m_ShowBenchWindow) return;

    ImGui::SetNextWindowPos({350, 10},  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({460, 0},  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Benchmark", &m_ShowBenchWindow)) { ImGui::End(); return; }

    if (m_BenchState == BenchmarkState::Idle || m_BenchState == BenchmarkState::Done) {
        ImGui::SeparatorText("Settings");
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Duration (s)",   &m_BenchDuration,   5, 120);
        ImGui::SameLine(); ImGui::TextDisabled("(+%.0fs warmup)", m_WarmupDuration);
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Random lights",  &m_BenchLightCount, 0,  64);
        ImGui::Spacing();
        ImGui::TextDisabled("Camera follows a pre-defined tour of the Sponza atrium.");
        ImGui::TextDisabled("Mouse and keyboard are locked during the run.");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.12f, 0.50f, 0.12f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.18f, 0.70f, 0.18f, 1.f});
        if (ImGui::Button("  Start Benchmark  ", {-FLT_MIN, 32}))
            StartBenchmark();
        ImGui::PopStyleColor(2);
    }

    if (m_BenchState == BenchmarkState::Warmup) {
        ImGui::SeparatorText("Warm-up");
        const float prog = m_WarmupElapsed / m_WarmupDuration;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.8f, 0.6f, 0.1f, 1.f});
        ImGui::ProgressBar(prog, {-FLT_MIN, 24});
        ImGui::PopStyleColor();
        ImGui::Text("Letting GPU settle  %.1f / %.0f s", m_WarmupElapsed, m_WarmupDuration);
    }

    if (m_BenchState == BenchmarkState::Running) {
        ImGui::SeparatorText("Running");
        const float prog = m_BenchElapsed / static_cast<float>(m_BenchDuration);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.2f, 0.8f, 0.3f, 1.f});
        ImGui::ProgressBar(prog, {-FLT_MIN, 24});
        ImGui::PopStyleColor();
        if (!m_BenchFrameTimes.empty()) {
            ImGui::Text("FPS %.1f  |  frame %u  |  %.1fs / %ds",
                        1000.f / m_BenchFrameTimes.back(),
                        static_cast<Manro::u32>(m_BenchFrameTimes.size()),
                        m_BenchElapsed, m_BenchDuration);
        }
        if (m_BenchFrameTimes.size() > 2) {
            const int dispN = static_cast<int>(
                    std::min(m_BenchFrameTimes.size(), static_cast<size_t>(300)));
            const int start = static_cast<int>(m_BenchFrameTimes.size()) - dispN;
            ImGui::PlotLines("##live",
                             m_BenchFrameTimes.data() + start, dispN,
                             0, nullptr, 0.f, 33.3f, {-FLT_MIN, 50});
        }
    }

    if (m_BenchState == BenchmarkState::Done) {
        ImGui::Spacing();
        ImGui::SeparatorText("Results");
        const auto& r = m_BenchResult;
        ImGui::PushStyleColor(ImGuiCol_Text, {0.4f, 1.f, 0.4f, 1.f});
        ImGui::Text("Avg %.2f FPS    Min %.2f    Max %.2f", r.avgFps, r.minFps, r.maxFps);
        ImGui::PopStyleColor();
        ImGui::Separator();

        if (ImGui::BeginTable("bt", 2,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value",  ImGuiTableColumnFlags_WidthFixed, 130.f);
            ImGui::TableHeadersRow();
            auto row = [](const char* label, const char* val) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(val);
            };
            char buf[64];
#define ROW(label, fmt, ...) snprintf(buf, sizeof(buf), fmt, __VA_ARGS__); row(label, buf)
            ROW("Avg frame time",    "%.3f ms", r.avgFrameMs);
            ROW("1%% low (99th)",    "%.3f ms", r.p1FrameMs);
            ROW("0.1%% low (99.9th)","%.3f ms", r.p01FrameMs);
            ROW("Total frames",      "%u",      r.totalFrames);
            ROW("Total time",        "%.2f s",  r.totalSeconds);
            ROW("Avg draw calls",    "%u",      r.avgDrawCalls);
            ROW("Avg triangles",     "%u",      r.avgTriangles);
            ROW("Random lights",     "%d",      m_BenchLightCount);
#undef ROW
            ImGui::EndTable();
        }

        if (!m_BenchFrameTimes.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Frame time distribution (ms):");
            const float maxMs = *std::ranges::max_element(m_BenchFrameTimes);
            ImGui::PlotHistogram("##bh",
                                 m_BenchFrameTimes.data(),
                                 static_cast<int>(m_BenchFrameTimes.size()),
                                 0, nullptr, 0.f, maxMs * 1.1f, {-FLT_MIN, 60});
        }

        ImGui::Spacing();
        if (ImGui::Button("Run Again")) StartBenchmark();
        ImGui::SameLine();
        if (ImGui::Button("Copy to Clipboard")) {
            char clip[700];
            snprintf(clip, sizeof(clip),
                     "--- Manro GPU Benchmark ---\n"
                     "Scene: Sponza  |  Duration: %.2fs  |  Frames: %u\n"
                     "Avg FPS: %.2f    Min: %.2f    Max: %.2f\n"
                     "Avg frame: %.3fms    1%% low: %.3fms    0.1%% low: %.3fms\n"
                     "Avg draw calls: %u    Avg triangles: %u",
                     r.totalSeconds, r.totalFrames,
                     r.avgFps, r.minFps, r.maxFps,
                     r.avgFrameMs, r.p1FrameMs, r.p01FrameMs,
                     r.avgDrawCalls, r.avgTriangles);
            ImGui::SetClipboardText(clip);
        }
    }

    ImGui::End();
}

void Sponza::StartBenchmark() {
    m_SavedCamPos   = m_Camera.Position;
    m_SavedCamYaw   = m_Camera.Yaw;
    m_SavedCamPitch = m_Camera.Pitch;

    m_PathT      = 0.f;
    m_PathSpeed  = static_cast<float>(kWaypointCount - 1) /
                   static_cast<float>(m_BenchDuration);

    m_BenchState         = BenchmarkState::Warmup;
    m_BenchElapsed       = 0.f;
    m_WarmupElapsed      = 0.f;
    m_BenchDrawCallsAcc  = 0;
    m_BenchTrianglesAcc  = 0;
    m_BenchResult        = {};
    m_BenchFrameTimes.clear();
    m_BenchFrameTimes.reserve(static_cast<size_t>(m_BenchDuration) * 500);
    m_InputCaptured = false;
    m_InputManager.ConsumeMouseDelta();

    m_BenchLights.clear();
    std::mt19937 rng(0xBEEF1234);
    std::uniform_real_distribution<float> rx(-1200.f, 1200.f);
    std::uniform_real_distribution<float> ry(50.f,    400.f);
    std::uniform_real_distribution<float> rz(-500.f,  500.f);
    std::uniform_real_distribution<float> rc(0.3f,    1.f);
    for (int i = 0; i < m_BenchLightCount; ++i) {
        Manro::LightData l{};
        l.type                  = shaderio::eLightTypePoint;
        l.position              = {rx(rng), ry(rng), rz(rng)};
        l.color                 = {rc(rng), rc(rng), rc(rng)};
        l.intensity             = 800.f;
        l.angularSizeOrInvRange = 1.f / 350.f;
        m_BenchLights.push_back(l);
    }
    LOG_INFO("[Benchmark] Warmup {}s then running {}s...",
             static_cast<int>(m_WarmupDuration), m_BenchDuration);
}

void Sponza::TickBenchmark(Manro::Renderer& renderer, float dt) {
    if (m_BenchState == BenchmarkState::Warmup) {
        m_WarmupElapsed += dt;
        if (m_WarmupElapsed >= m_WarmupDuration) {
            m_BenchState    = BenchmarkState::Running;
            m_BenchElapsed  = 0.f;
            LOG_INFO("[Benchmark] Warmup done, measuring...");
        }
        return;
    }

    m_BenchFrameTimes.push_back(dt * 1000.f);
    m_BenchDrawCallsAcc += m_LastStats.drawCalls;
    m_BenchTrianglesAcc += m_LastStats.triangleCount;
    m_BenchElapsed      += dt;

    if (m_BenchElapsed >= static_cast<float>(m_BenchDuration))
        FinishBenchmark(renderer);
}

void Sponza::FinishBenchmark(Manro::Renderer& /*renderer*/) {
    auto& r        = m_BenchResult;
    r.totalFrames  = static_cast<Manro::u32>(m_BenchFrameTimes.size());
    r.totalSeconds = m_BenchElapsed;
    r.avgDrawCalls = static_cast<Manro::u32>(
            m_BenchDrawCallsAcc / std::max(r.totalFrames, 1u));
    r.avgTriangles = static_cast<Manro::u32>(
            m_BenchTrianglesAcc / std::max(r.totalFrames, 1u));

    const float sum = std::accumulate(
            m_BenchFrameTimes.begin(), m_BenchFrameTimes.end(), 0.f);
    r.avgFrameMs = sum / static_cast<float>(r.totalFrames);
    r.avgFps     = 1000.f / r.avgFrameMs;

    const float minMs = *std::ranges::min_element(m_BenchFrameTimes);
    const float maxMs = *std::ranges::max_element(m_BenchFrameTimes);
    r.minFps = 1000.f / maxMs;
    r.maxFps = 1000.f / minMs;

    std::vector<float> sorted = m_BenchFrameTimes;
    std::ranges::sort(sorted);
    auto pct = [&](float p) -> float {
        const size_t i = static_cast<size_t>(p * static_cast<float>(sorted.size() - 1));
        return sorted[std::min(i, sorted.size() - 1)];
    };
    r.p1FrameMs  = pct(0.99f);
    r.p01FrameMs = pct(0.999f);

    m_BenchState      = BenchmarkState::Done;
    m_Camera.Position = m_SavedCamPos;
    m_Camera.Yaw      = m_SavedCamYaw;
    m_Camera.Pitch    = m_SavedCamPitch;
    m_InputCaptured   = true;

    LOG_INFO("[Benchmark] Done  {:.2f} avg FPS  |  {:.3f}ms avg  |"
             "  {:.3f}ms 1% low  |  {:.3f}ms 0.1% low",
             r.avgFps, r.avgFrameMs, r.p1FrameMs, r.p01FrameMs);
}

void Sponza::AdvanceBenchCamera(float dt) {
    m_PathT += m_PathSpeed * dt;
    const int loopCount = kWaypointCount - 1;
    while (m_PathT >= static_cast<float>(loopCount))
        m_PathT -= static_cast<float>(loopCount);

    const int   seg = static_cast<int>(m_PathT);
    const float t   = m_PathT - static_cast<float>(seg);

    auto wp = [&](int i) -> const BenchWaypoint& {
        i = std::max(0, std::min(kWaypointCount - 1, i));
        return kWaypoints[i];
    };

    m_Camera.Position = CatmullRomPos(wp(seg-1), wp(seg), wp(seg+1), wp(seg+2), t);
    m_Camera.Yaw      = CatmullRomAngle(wp(seg-1).yaw,   wp(seg).yaw,
                                        wp(seg+1).yaw,   wp(seg+2).yaw,   t);
    m_Camera.Pitch    = CatmullRomAngle(wp(seg-1).pitch,  wp(seg).pitch,
                                        wp(seg+1).pitch,  wp(seg+2).pitch,  t);
}

Manro::Vec3 Sponza::CatmullRomPos(const BenchWaypoint& p0, const BenchWaypoint& p1,
                                  const BenchWaypoint& p2, const BenchWaypoint& p3,
                                  float t) {
    const float t2 = t * t, t3 = t2 * t;
    const Manro::Vec3 a = -0.5f*p0.position + 1.5f*p1.position - 1.5f*p2.position + 0.5f*p3.position;
    const Manro::Vec3 b =  p0.position - 2.5f*p1.position + 2.f*p2.position - 0.5f*p3.position;
    const Manro::Vec3 c = -0.5f*p0.position + 0.5f*p2.position;
    return a*t3 + b*t2 + c*t + p1.position;
}

float Sponza::CatmullRomAngle(float a0, float a1, float a2, float a3, float t) {
    auto unwrap = [](float base, float angle) {
        while (angle - base >  180.f) angle -= 360.f;
        while (angle - base < -180.f) angle += 360.f;
        return angle;
    };
    a2 = unwrap(a1, a2); a3 = unwrap(a2, a3); a0 = unwrap(a1, a0);
    const float t2 = t*t, t3 = t2*t;
    return (-0.5f*a0 + 1.5f*a1 - 1.5f*a2 + 0.5f*a3)*t3
           + (a0 - 2.5f*a1 + 2.f*a2 - 0.5f*a3)*t2
           + (-0.5f*a0 + 0.5f*a2)*t
           + a1;
}

Manro::Vec3 FlyCamera::Forward() const {
    const float yR = glm::radians(Yaw), pR = glm::radians(Pitch);
    return glm::normalize(Manro::Vec3{cosf(pR)*cosf(yR), sinf(pR), cosf(pR)*sinf(yR)});
}

void FlyCamera::Update(const Manro::InputManager& input, float dt) {
    using K = Manro::Key;
    auto [x, y] = const_cast<Manro::InputManager&>(input).ConsumeMouseDelta();
    Yaw   += x * MouseSensitivity;
    Pitch  = std::clamp(Pitch - y * MouseSensitivity, -89.f, 89.f);

    const Manro::Vec3 fwd   = Forward();
    const Manro::Vec3 right = glm::normalize(glm::cross(fwd, Manro::Vec3{0,1,0}));
    const Manro::Vec3 up    = {0, 1, 0};
    const float       speed = input.IsKeyDown(K::LeftShift) ? SprintSpeed : NormalSpeed;

    Manro::Vec3 move{0};
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
    return glm::lookAt(Position, Position + Forward(), {0,1,0});
}

Manro::Mat4 FlyCamera::Projection(float fovDeg, float aspect, float nearZ, float farZ) {
    return glm::perspective(glm::radians(fovDeg), aspect, nearZ, farZ);
}

const BenchWaypoint Sponza::kWaypoints[] = {
        {{-1200.f, 150.f,   0.f}, -90.f,  -8.f},
        {{ -700.f, 150.f,   0.f}, -90.f,  -5.f},
        {{ -200.f, 200.f,  40.f}, -60.f, -15.f},
        {{    0.f, 350.f,   0.f}, -90.f, -35.f},
        {{  200.f, 200.f, -40.f},-120.f, -15.f},
        {{  700.f, 150.f,   0.f}, -90.f,  -5.f},
        {{ 1200.f, 150.f,   0.f}, -90.f,  -8.f},
        {{  900.f, 120.f, 300.f},-160.f,  -5.f},
        {{    0.f, 120.f, 500.f}, 180.f,  -8.f},
        {{ -900.f, 120.f, 300.f}, 160.f,  -5.f},
        {{-1000.f, 400.f,   0.f}, -70.f, -20.f},
        {{    0.f, 500.f,   0.f}, -90.f, -50.f},
        {{ 1000.f, 400.f,   0.f},-110.f, -20.f},
        {{  600.f,  80.f,-400.f},  10.f,  -3.f},
        {{    0.f,  80.f,-600.f},   0.f,  -3.f},
        {{ -600.f,  80.f,-400.f}, -10.f,  -3.f},
        {{-1200.f, 150.f,   0.f}, -90.f,  -8.f},
};
const int Sponza::kWaypointCount = 17;