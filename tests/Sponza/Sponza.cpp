#include "Sponza.h"

#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Render/Gui/ImGuiLayer.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>

static constexpr auto kSponzaPath = "models/sponza/Sponza.gltf";
static float kFov = 100.f;
static constexpr float kNearZ = 1.f;
static constexpr float kFarZ = 10000.f;
static constexpr Manro::u32 kWindowWidth = 1920;
static constexpr Manro::u32 kWindowHeight = 1080;

static float s_TimeOfDay = 10.0f;
static bool s_AnimateSun = true;
static float s_DaySpeed = 0.5f;

Manro::Vec3 FlyCamera::Forward() const {
    const float yR = glm::radians(Yaw);
    const float pR = glm::radians(Pitch);
    return glm::normalize(Manro::Vec3{
        cosf(pR)*cosf(yR), sinf(pR), cosf(pR)*sinf(yR)});
}

void FlyCamera::Update(const Manro::InputManager& input, float dt) {
    using K = Manro::Key;
    auto [x, y] = const_cast<Manro::InputManager &>(input).ConsumeMouseDelta();
    Yaw += x * MouseSensitivity;
    Pitch = std::clamp(Pitch - y * MouseSensitivity, -89.f, 89.f);

    const Manro::Vec3 fwd = Forward();
    Manro::Vec3 right;
    right = glm::normalize(glm::cross(fwd, Manro::Vec3{0, 1, 0}));
    Manro::Vec3 up = {0, 1, 0};
    float speed = input.IsKeyDown(K::LeftShift) ? SprintSpeed : NormalSpeed;

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

void Sponza::Initialize() {
    auto &wm = m_Engine.GetPlatform().GetWindowManager();
    Manro::WindowDesc desc;
    desc.Title = "Sponza Test";
    desc.Width = kWindowWidth;
    desc.Height = kWindowHeight;
    desc.Fullscreen = false;
    m_Window = wm.AddWindow(desc);
    if (m_Window == Manro::kInvalidWindow)
        throw std::runtime_error("[SponzaTest] Failed to create window.");

    wm.Get(m_Window)->SetEventCallback(
        [this](Manro::WindowEvent ev, Manro::u32 w, Manro::u32 h) {
            if (ev == Manro::WindowEvent::Close) m_IsRunning = false;
            else if (ev == Manro::WindowEvent::Resized && m_Renderer)
                m_Renderer->OnResize(w, h);
        });

    m_InputManager.SetBackend(&m_InputBackend);
    wm.Get(m_Window)->CaptureMouse(true);
    wm.Get(m_Window)->ShowCursor(false);

    Manro::VirtualFS::Get().SetBaseDir(MANRO_ASSETS_DIR);
    Manro::RegisterEmbeddedShaders();

    Manro::RenderSettings settings{};
    settings.msaaSamples = VK_SAMPLE_COUNT_8_BIT;
    settings.enableVSync = false;

    m_Renderer = Manro::CreateScope<Manro::Renderer>(*wm.Get(m_Window), kWindowWidth, kWindowHeight, settings);
    LOG_INFO("[SponzaTest] Renderer initialized.");

    LoadScene();
    m_IsRunning     = true;
    m_LastFrameTime = std::chrono::high_resolution_clock::now();
    LOG_INFO("[SponzaTest] Ready.  WASD=move  Mouse=look  Shift=sprint  Q/E=up/down  Escape=quit");
}

void Sponza::Run() {
    auto &platform = m_Engine.GetPlatform();
    while (m_IsRunning) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - m_LastFrameTime).count();
        m_LastFrameTime = now;
        if (dt > 0.1f) dt = 0.1f;
        m_AccumulatedTime += dt;

        if (s_AnimateSun) {
            s_TimeOfDay += dt * s_DaySpeed;
            if (s_TimeOfDay >= 24.0f) s_TimeOfDay -= 24.0f;
        }

        if (!platform.PollEvents(&m_InputManager)) m_IsRunning = false;

        if (m_BenchState == BenchmarkState::Idle || m_BenchState == BenchmarkState::Done) {
            bool ctrl = m_InputManager.IsKeyDown(Manro::Key::LeftCtrl);
            bool f11 = m_InputManager.IsKeyDown(Manro::Key::F11);
            auto *win = platform.GetWindowManager().Get(m_Window);
            if (ctrl && !m_CtrlPressedLastFrame) {
                m_InputCaptured = !m_InputCaptured;
                if (win) {
                    win->CaptureMouse(m_InputCaptured);
                    win->ShowCursor(!m_InputCaptured);
                }
            }
            if (f11 && !m_f11PressedLastFrame) {
                m_InputCaptured = !m_InputCaptured;
                if (win && !win->IsFullscreen())
                    win->SetFullscreen(true);
                else
                    win->SetFullscreen(false);
            }
            m_CtrlPressedLastFrame = ctrl;
            m_f11PressedLastFrame = f11;
        }

        if (m_InputManager.IsKeyDown(Manro::Key::Escape)) {
            if (m_BenchState == BenchmarkState::Running ||
                m_BenchState == BenchmarkState::Warmup) {
                m_BenchState = BenchmarkState::Idle;
                m_BenchFrameTimes.clear();
                m_Camera.Position = m_SavedCamPos;
                m_Camera.Yaw = m_SavedCamYaw;
                m_Camera.Pitch = m_SavedCamPitch;
                if (auto *win = platform.GetWindowManager().Get(m_Window)) {
                    win->CaptureMouse(true);
                    win->ShowCursor(false);
                }
                m_InputCaptured = true;
            } else {
                m_IsRunning = false;
            }
        }

        if (m_BenchState == BenchmarkState::Warmup ||
            m_BenchState == BenchmarkState::Running) {
            TickBenchmark(dt);
        } else if (m_InputCaptured) {
            m_Camera.Update(m_InputManager, dt);
        } else {
            m_InputManager.ConsumeMouseDelta();
        }

        Render(dt);
    }
}

void Sponza::Shutdown() {
    m_Is7Model.reset();
    m_Model.reset();
    m_Renderer.reset();
    m_IsRunning = false;
}

void Sponza::LoadScene() {
    auto models = Manro::Model::Load({kSponzaPath}, *m_Renderer, m_Engine.GetJobSystem());
    if (models.empty() || !models[0]) {
        LOG_ERROR("[SponzaTest] Failed to load Sponza!");
        return;
    }
    m_Model = std::move(models[0]);
    if (m_Model) m_Renderer->DrawModelStatic(*m_Model, glm::scale(glm::mat4(1.0f), glm::vec3(100.0f)));
}

void Sponza::Render(const float dt) {
    if (!m_Renderer) return;
    if (!m_Renderer->BeginFrame()) return;

    float aspect = m_Renderer->GetAspectRatio();
    m_Renderer->SetViewProjection(
        m_Camera.View(),
        m_Camera.Projection(kFov, aspect, kNearZ, kFarZ));

    m_Renderer->ClearLights();

    float dayTau = (s_TimeOfDay / 24.0f) * 2.0f * 3.14159265f;
    float sunAltitude = sinf(dayTau - 1.5707f);
    float sunAzimuth = cosf(dayTau - 1.5707f);

    Manro::LightData sun{};
    sun.type = shaderio::eLightTypeDirectional;

    sun.direction = glm::normalize(Manro::Vec3{sunAzimuth, -sunAltitude, 0.3f});

    if (sunAltitude > 0.05f) {
        sun.color = {1.0f, 0.98f, 0.90f};
        sun.intensity = 3.0f * sunAltitude;
    } else if (sunAltitude > -0.1f) {
        float t = (sunAltitude + 0.1f) / 0.15f;
        sun.color = glm::mix(Manro::Vec3{1.0f, 0.3f, 0.05f}, Manro::Vec3{1.0f, 0.98f, 0.90f}, t);
        sun.intensity = 0.8f;
    } else {
        sun.direction = glm::normalize(Manro::Vec3{-sunAzimuth, sunAltitude, -0.3f});
        sun.color = {0.1f, 0.15f, 0.35f};
        sun.intensity = 0.2f;
    }

    m_Renderer->AddLight(sun);

    if (m_BenchState == BenchmarkState::Warmup ||
        m_BenchState == BenchmarkState::Running) {
        for (const auto& l : m_BenchLights)
            m_Renderer->AddLight(l);
    }


    Manro::Vec4 clearColor = {0.02f, 0.02f, 0.05f, 1.f};
    if (sunAltitude > 0.0f) {
        clearColor = glm::mix(Manro::Vec4{0.1f, 0.05f, 0.02f, 1.f}, Manro::Vec4{0.4f, 0.6f, 0.9f, 1.f}, sunAltitude);
    }

    m_Renderer->BeginRendering(clearColor);
    m_Renderer->RenderQueue();
    m_Renderer->EndRendering();

    DrawGui(dt);

    m_Renderer->EndFrameAndPresent();

    m_LastStats = m_Renderer->GetLastFrameStats();
    float frameMs = dt * 1000.f;
    m_FrameTimeHistory[m_FrameTimeOffset] = frameMs;
    m_FrameTimeOffset = (m_FrameTimeOffset + 1) % kHistoryLen;
}

void Sponza::DrawGui(const float dt) {
    float fps = dt > 0.f ? 1.f / dt : 0.f;
    float msdt = dt * 1000.f;

    bool benchActive = (m_BenchState == BenchmarkState::Warmup ||
                        m_BenchState == BenchmarkState::Running);

    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
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
            Manro::RenderSettings settings = m_Renderer->GetSettings();
            bool changed = false;

            if (ImGui::SliderFloat("Resolution Scale", &settings.resolutionScale, 0.1f, 2.0f)) changed = true;
            if (ImGui::Checkbox("VSync", &settings.enableVSync)) changed = true;
            if (ImGui::Checkbox("Frustum Culling", &settings.enableFrustumCulling)) changed = true;

            if (ImGui::TreeNode("Camera")) {
                if (ImGui::DragFloat("Near Z", &settings.nearZ, 0.01f, 0.001f, 10.0f)) changed = true;
                if (ImGui::DragFloat("Far Z", &settings.farZ, 10.0f, 100.0f, 100000.0f)) changed = true;
                if (ImGui::SliderFloat("FoV", &kFov, 50.f, 120.f)) changed = true;
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Environment & Sun")) {
                ImGui::Checkbox("Animate Day Cycle", &s_AnimateSun);
                ImGui::SliderFloat("Time", &s_TimeOfDay, 0.0f, 24.0f, "%.1f h");
                ImGui::SliderFloat("Day Speed", &s_DaySpeed, 0.0f, 5.0f);
                ImGui::Separator();
                if (ImGui::SliderFloat("IBL Intensity", &settings.lighting.iblIntensity, 0.0f, 5.0f)) changed = true;
                if (ImGui::SliderFloat("Gamma", &settings.lighting.gamma, 1.0f, 3.0f)) changed = true;
                if (ImGui::Checkbox("AO Enabled", &settings.lighting.enableAmbientOcclusion)) changed = true;
                if (settings.lighting.enableAmbientOcclusion) {
                    if (ImGui::SliderFloat("AO Intensity", &settings.lighting.aoIntensity, 0.0f, 2.0f)) changed = true;
                    if (ImGui::SliderFloat("AO Radius", &settings.lighting.aoRadius, 0.01f, 2.0f)) changed = true;
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Shadows")) {
                if (ImGui::Checkbox("Enabled##Shadows", &settings.shadows.enabled)) changed = true;
                if (ImGui::DragInt("Resolution", &settings.shadows.resolution, 128, 128, 4096)) changed = true;
                if (ImGui::SliderFloat("Bias", &settings.shadows.bias, 0.0f, 0.05f, "%.4f")) changed = true;
                if (ImGui::SliderFloat("Slope Bias", &settings.shadows.slopeBias, 0.0f, 0.5f)) changed = true;
                if (ImGui::SliderFloat("Softness", &settings.shadows.softShadows, 0.0f, 5.0f)) changed = true;
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Post-Processing")) {
                auto& tm = settings.postProcess.tonemapping;
                if (ImGui::SliderFloat("Exposure", &tm.exposure, 0.0f, 10.0f)) changed = true;
                if (ImGui::SliderFloat("Contrast", &tm.contrast, 0.0f, 3.0f)) changed = true;
                if (ImGui::SliderFloat("Saturation", &tm.saturation, 0.0f, 3.0f)) changed = true;

                const char* methods[] = { "Filmic", "Uncharted2", "Clip", "ACES", "AgX", "KhronosPBR" };
                if (ImGui::Combo("Method", &tm.method, methods, IM_ARRAYSIZE(methods))) changed = true;

                ImGui::TreePop();
            }

            if (changed) m_Renderer->SetSettings(settings);
        }

        if (ImGui::CollapsingHeader("GPU")) {
            Manro::u64 used, budget;
            m_Renderer->GetContext().GetVramStats(used, budget);
            float usedMB = static_cast<float>(used) / (1024.f * 1024.f);
            float budgetMB = static_cast<float>(budget) / (1024.f * 1024.f);
            ImGui::Text("VRAM  %.1f / %.1f MB", usedMB, budgetMB);
            ImGui::ProgressBar(usedMB / std::max(budgetMB, 1.f), {-FLT_MIN, 0});
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(
                m_Renderer->GetContext().GetPhysicalDevice(), &props);
            ImGui::TextDisabled("%s", props.deviceName);
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (benchActive) {
            if (m_BenchState == BenchmarkState::Warmup) {
                float prog = m_WarmupElapsed / m_WarmupDuration;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.8f, 0.6f, 0.1f, 1.f});
                ImGui::ProgressBar(prog, {-FLT_MIN, 0}, "Warming up...");
                ImGui::PopStyleColor();
                ImGui::Text("%.1f / %.0fs", m_WarmupElapsed, m_WarmupDuration);
            } else {
                float prog = m_BenchElapsed / static_cast<float>(m_BenchDuration);
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

    ImGui::SetNextWindowPos({350, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({460, 0}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Benchmark", &m_ShowBenchWindow)) {
        ImGui::End();
        return;
    }

    if (!benchActive) {
        ImGui::SeparatorText("Settings");
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Duration (s)", &m_BenchDuration, 5, 120);
        ImGui::SameLine();
        ImGui::TextDisabled("(+%.0fs warmup)", m_WarmupDuration);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Random lights", &m_BenchLightCount, 0, 64);
        ImGui::SameLine();
        ImGui::TextDisabled("(scattered around atrium)");

        ImGui::Spacing();
        ImGui::TextDisabled("Camera will follow a pre-defined tour of the Sponza atrium.");
        ImGui::TextDisabled("Mouse and keyboard are locked during the run.");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, {0.12f, 0.50f, 0.12f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.18f, 0.70f, 0.18f, 1.f});
        if (ImGui::Button("  Start Benchmark  ", {-FLT_MIN, 32}))
            StartBenchmark();
        ImGui::PopStyleColor(2);
    }

    if (m_BenchState == BenchmarkState::Warmup) {
        ImGui::SeparatorText("Warm-up");
        float prog = m_WarmupElapsed / m_WarmupDuration;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.8f, 0.6f, 0.1f, 1.f});
        ImGui::ProgressBar(prog, {-FLT_MIN, 24});
        ImGui::PopStyleColor();
        ImGui::Text("Letting GPU settle  %.1f / %.0f s", m_WarmupElapsed, m_WarmupDuration);
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, {0.50f, 0.10f, 0.10f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.70f, 0.15f, 0.15f, 1.f});
        if (ImGui::Button("Cancel", {-FLT_MIN, 0})) {
            m_BenchState = BenchmarkState::Idle;
            m_BenchFrameTimes.clear();
            m_Camera.Position = m_SavedCamPos;
            m_Camera.Yaw = m_SavedCamYaw;
            m_Camera.Pitch = m_SavedCamPitch;
            if (auto *win = m_Engine.GetPlatform().GetWindowManager().Get(m_Window)) {
                win->CaptureMouse(true);
                win->ShowCursor(false);
            }
            m_InputCaptured = true;
        }
        ImGui::PopStyleColor(2);
    }

    if (m_BenchState == BenchmarkState::Running) {
        ImGui::SeparatorText("Running");
        float prog = m_BenchElapsed / static_cast<float>(m_BenchDuration);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.2f, 0.8f, 0.3f, 1.f});
        ImGui::ProgressBar(prog, {-FLT_MIN, 24});
        ImGui::PopStyleColor();

        if (!m_BenchFrameTimes.empty()) {
            float curFps = 1000.f / m_BenchFrameTimes.back();
            ImGui::Text("FPS %.1f  |  frame %u  |  %.1fs / %ds",
                        curFps, static_cast<Manro::u32>(m_BenchFrameTimes.size()),
                        m_BenchElapsed, m_BenchDuration);
        }

        if (m_BenchFrameTimes.size() > 2) {
            int dispN = static_cast<int>(std::min(m_BenchFrameTimes.size(), static_cast<size_t>(300)));
            int start = static_cast<int>(m_BenchFrameTimes.size()) - dispN;
            ImGui::PlotLines("##live",
                             m_BenchFrameTimes.data() + start, dispN,
                             0, nullptr, 0.f, 33.3f, {-FLT_MIN, 50});
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, {0.50f, 0.10f, 0.10f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.70f, 0.15f, 0.15f, 1.f});
        if (ImGui::Button("Cancel", {-FLT_MIN, 0})) {
            m_BenchState = BenchmarkState::Idle;
            m_BenchFrameTimes.clear();
            m_Camera.Position = m_SavedCamPos;
            m_Camera.Yaw = m_SavedCamYaw;
            m_Camera.Pitch = m_SavedCamPitch;
            if (auto *win = m_Engine.GetPlatform().GetWindowManager().Get(m_Window)) {
                win->CaptureMouse(true);
                win->ShowCursor(false);
            }
            m_InputCaptured = true;
        }
        ImGui::PopStyleColor(2);
    }

    if (m_BenchState == BenchmarkState::Done) {
        ImGui::Spacing();
        ImGui::SeparatorText("Results");

        const auto &r = m_BenchResult;
        ImGui::PushStyleColor(ImGuiCol_Text, {0.4f, 1.f, 0.4f, 1.f});
        ImGui::Text("Avg %.2f FPS    Min %.2f    Max %.2f",
                    r.avgFps, r.minFps, r.maxFps);
        ImGui::PopStyleColor();
        ImGui::Separator();

        if (ImGui::BeginTable("bt", 2,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 130.f);
            ImGui::TableHeadersRow();

            auto row = [](const char *label, const char *val) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(val);
            };
            char buf[64];
#define ROW(label, fmt, ...) snprintf(buf,sizeof(buf),fmt,__VA_ARGS__); row(label,buf)
            ROW("Avg frame time", "%.3f ms", r.avgFrameMs);
            ROW("1%% low  (99th pct)", "%.3f ms", r.p1FrameMs);
            ROW("0.1%% low (99.9th)", "%.3f ms", r.p01FrameMs);
            ROW("Total frames", "%u", r.totalFrames);
            ROW("Total time", "%.2f s", r.totalSeconds);
            ROW("Avg draw calls", "%u", r.avgDrawCalls);
            ROW("Avg triangles", "%u", r.avgTriangles);
            ROW("Random lights", "%d", m_BenchLightCount);
#undef ROW
            ImGui::EndTable();
        }

        if (!m_BenchFrameTimes.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Frame time distribution (ms):");
            float maxMs = *std::ranges::max_element(m_BenchFrameTimes);
            ImGui::PlotHistogram("##bh",
                                 m_BenchFrameTimes.data(), static_cast<int>(m_BenchFrameTimes.size()),
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
    m_SavedCamPos = m_Camera.Position;
    m_SavedCamYaw = m_Camera.Yaw;
    m_SavedCamPitch = m_Camera.Pitch;

    m_PathT = 0.f;
    m_PathSpeed = static_cast<float>(kWaypointCount - 1) / static_cast<float>(m_BenchDuration);

    m_BenchState = BenchmarkState::Warmup;
    m_BenchElapsed = 0.f;
    m_WarmupElapsed = 0.f;
    m_BenchDrawCallsAcc = 0;
    m_BenchTrianglesAcc = 0;
    m_BenchResult = {};
    m_BenchFrameTimes.clear();
    m_BenchFrameTimes.reserve(static_cast<size_t>(m_BenchDuration) * 500);

    if (auto *win = m_Engine.GetPlatform().GetWindowManager().Get(m_Window)) {
        win->CaptureMouse(false);
        win->ShowCursor(true);
    }
    m_InputCaptured = false;
    m_InputManager.ConsumeMouseDelta();

    m_BenchLights.clear();
    std::mt19937 rng(0xBEEF1234);
    std::uniform_real_distribution<float> rx(-1200.f, 1200.f);
    std::uniform_real_distribution<float> ry(50.f, 400.f);
    std::uniform_real_distribution<float> rz(-500.f, 500.f);
    std::uniform_real_distribution<float> rc(0.3f, 1.f);
    for (int i = 0; i < m_BenchLightCount; ++i) {
        Manro::LightData l{};
        l.type = shaderio::eLightTypePoint;
        l.position = {rx(rng), ry(rng), rz(rng)};
        l.color = {rc(rng), rc(rng), rc(rng)};
        l.intensity = 800.f;
        l.angularSizeOrInvRange = 1.f / 350.f;
        m_BenchLights.push_back(l);
    }
    LOG_INFO("[Benchmark] Warmup {}s then running {}s...",
             static_cast<int>(m_WarmupDuration), m_BenchDuration);
}

void Sponza::TickBenchmark(float dt) {
    AdvanceBenchCamera(dt);

    if (m_BenchState == BenchmarkState::Warmup) {
        m_WarmupElapsed += dt;
        if (m_WarmupElapsed >= m_WarmupDuration) {
            m_BenchState = BenchmarkState::Running;
            m_BenchElapsed = 0.f;
            LOG_INFO("[Benchmark] Warmup done, measuring...");
        }
        return;
    }

    float ms = dt * 1000.f;
    m_BenchFrameTimes.push_back(ms);
    m_BenchDrawCallsAcc += m_LastStats.drawCalls;
    m_BenchTrianglesAcc += m_LastStats.triangleCount;
    m_BenchElapsed += dt;

    if (m_BenchElapsed >= static_cast<float>(m_BenchDuration))
        FinishBenchmark();
}

void Sponza::FinishBenchmark() {
    auto &r = m_BenchResult;
    r.totalFrames = static_cast<Manro::u32>(m_BenchFrameTimes.size());
    r.totalSeconds = m_BenchElapsed;
    r.avgDrawCalls = static_cast<Manro::u32>(m_BenchDrawCallsAcc / std::max(r.totalFrames, 1u));
    r.avgTriangles = static_cast<Manro::u32>(m_BenchTrianglesAcc / std::max(r.totalFrames, 1u));

    float sum = std::accumulate(m_BenchFrameTimes.begin(), m_BenchFrameTimes.end(), 0.f);
    r.avgFrameMs = sum / static_cast<float>(r.totalFrames);
    r.avgFps = 1000.f / r.avgFrameMs;

    float minMs = *std::ranges::min_element(m_BenchFrameTimes);
    float maxMs = *std::ranges::max_element(m_BenchFrameTimes);
    r.minFps = 1000.f / maxMs;
    r.maxFps = 1000.f / minMs;

    std::vector<float> sorted = m_BenchFrameTimes;
    std::ranges::sort(sorted);
    auto pct = [&](float p) -> float {
        size_t i = (size_t) (p * static_cast<float>(sorted.size() - 1));
        return sorted[std::min(i, sorted.size() - 1)];
    };
    r.p1FrameMs = pct(0.99f);
    r.p01FrameMs = pct(0.999f);

    m_BenchState = BenchmarkState::Done;

    m_Camera.Position = m_SavedCamPos;
    m_Camera.Yaw = m_SavedCamYaw;
    m_Camera.Pitch = m_SavedCamPitch;

    if (auto *win = m_Engine.GetPlatform().GetWindowManager().Get(m_Window)) {
        win->CaptureMouse(true);
        win->ShowCursor(false);
    }
    m_InputCaptured = true;

    LOG_INFO("[Benchmark] Done  {:.2f} avg FPS  |  {:.3f}ms avg  |  "
             "{:.3f}ms 1% low  |  {:.3f}ms 0.1% low",
             r.avgFps, r.avgFrameMs, r.p1FrameMs, r.p01FrameMs);
}

void Sponza::AdvanceBenchCamera(float dt) {
    m_PathT += m_PathSpeed * dt;

    int loopCount = kWaypointCount - 1;
    while (m_PathT >= (float) loopCount)
        m_PathT -= static_cast<float>(loopCount);

    int seg = static_cast<int>(m_PathT);
    float t = m_PathT - static_cast<float>(seg);

    auto wp = [&](int i) -> const BenchWaypoint & {
        i = std::max(0, std::min(kWaypointCount - 1, i));
        return kWaypoints[i];
    };

    const BenchWaypoint &p0 = wp(seg - 1);
    const BenchWaypoint &p1 = wp(seg);
    const BenchWaypoint &p2 = wp(seg + 1);
    const BenchWaypoint &p3 = wp(seg + 2);

    m_Camera.Position = CatmullRomPos(p0, p1, p2, p3, t);
    m_Camera.Yaw = CatmullRomAngle(p0.yaw, p1.yaw, p2.yaw, p3.yaw, t);
    m_Camera.Pitch = CatmullRomAngle(p0.pitch, p1.pitch, p2.pitch, p3.pitch, t);
}

Manro::Vec3 Sponza::CatmullRomPos(const BenchWaypoint &p0, const BenchWaypoint &p1,
                                  const BenchWaypoint &p2, const BenchWaypoint &p3,
                                  const float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    Manro::Vec3 a = -0.5f * p0.position + 1.5f * p1.position - 1.5f * p2.position + 0.5f * p3.position;
    Manro::Vec3 b = p0.position - 2.5f * p1.position + 2.0f * p2.position - 0.5f * p3.position;
    Manro::Vec3 c = -0.5f * p0.position + 0.5f * p2.position;
    Manro::Vec3 d = p1.position;
    return a * t3 + b * t2 + c * t + d;
}

float Sponza::CatmullRomAngle(float a0, float a1, float a2, float a3, float t) {
    auto unwrap = [](float base, float angle) {
        while (angle - base > 180.f) angle -= 360.f;
        while (angle - base < -180.f) angle += 360.f;
        return angle;
    };
    a2 = unwrap(a1, a2);
    a3 = unwrap(a2, a3);
    a0 = unwrap(a1, a0);

    float t2 = t * t, t3 = t2 * t;
    float a = -0.5f * a0 + 1.5f * a1 - 1.5f * a2 + 0.5f * a3;
    float b = a0 - 2.5f * a1 + 2.0f * a2 - 0.5f * a3;
    float c = -0.5f * a0 + 0.5f * a2;
    float d = a1;
    return a * t3 + b * t2 + c * t + d;
}

const BenchWaypoint Sponza::kWaypoints[] = {
    {{-1200.f, 150.f, 0.f}, -90.f, -8.f}, // 0  west end, heading east
    {{-700.f, 150.f, 0.f}, -90.f, -5.f}, // 1  mid-west corridor
    {{-200.f, 200.f, 40.f}, -60.f, -15.f}, // 2  centre-west, looking up-right
    {{0.f, 350.f, 0.f}, -90.f, -35.f}, // 3  atrium centre elevated
    {{200.f, 200.f, -40.f}, -120.f, -15.f}, // 4  centre-east, looking up-left
    {{700.f, 150.f, 0.f}, -90.f, -5.f}, // 5  mid-east corridor
    {{1200.f, 150.f, 0.f}, -90.f, -8.f}, // 6  east end
    {{900.f, 120.f, 300.f}, -160.f, -5.f}, // 7  north-east corner
    {{0.f, 120.f, 500.f}, 180.f, -8.f}, // 8  north wall, facing south
    {{-900.f, 120.f, 300.f}, 160.f, -5.f}, // 9  north-west corner
    {{-1000.f, 400.f, 0.f}, -70.f, -20.f}, // 10 high west, wide angle
    {{0.f, 500.f, 0.f}, -90.f, -50.f}, // 11 high centre, looking down
    {{1000.f, 400.f, 0.f}, -110.f, -20.f}, // 12 high east
    {{600.f, 80.f, -400.f}, 10.f, -3.f}, // 13 south-east ground, facing north
    {{0.f, 80.f, -600.f}, 0.f, -3.f}, // 14 south ground, close arches
    {{-600.f, 80.f, -400.f}, -10.f, -3.f}, // 15 south-west ground
    {{-1200.f, 150.f, 0.f}, -90.f, -8.f}, // 16 = waypoint 0 (loop close)
};

const int Sponza::kWaypointCount = 17;