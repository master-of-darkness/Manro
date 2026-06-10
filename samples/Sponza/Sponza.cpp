#include "Sponza.h"

#include <Manro/Platform/Window/Window.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Resource/RresMount.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Manro/Core/Logger.h>
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <numeric>
#include <cmath>
#include <cstdio>
#include <random>

static float kFov = 100.f;
static constexpr float kNearZ = 1.f;
static constexpr float kFarZ = 10000.f;
static constexpr Manro::u32 kWindowWidth = 1920;
static constexpr Manro::u32 kWindowHeight = 1080;

static float s_TimeOfDay = 10.0f;
static bool s_AnimateSun = true;
static float s_DaySpeed = 0.5f;

Manro::WindowDesc_t CSponza::GetWindowDesc() const {
    Manro::WindowDesc_t d;
    d.Title = "CSponza";
    d.Width = kWindowWidth;
    d.Height = kWindowHeight;
    d.Fullscreen = false;
    d.Resizable = true;
    return d;
}

void CSponza::OnStartup(const Manro::InitContext_t &ctx) {
    m_Window = &ctx.CWindow;
    m_Jobs = &ctx.Jobs;
    m_Renderer = &ctx.CRenderer;
    m_Vfs = &ctx.Vfs;
    m_Renderer->SetDebugUIEnabled(true);

    const auto worldDir =
            (std::filesystem::path(MANRO_ASSETS_DIR) / "../../world")
            .lexically_normal().string();
    m_Vfs->SetBaseDir(worldDir);

    const std::string rresPath = worldDir + "/scenes/test.rres";
    if (std::filesystem::exists(rresPath)) {
        LOG_INFO("[CSponza] Mounting archive {}", rresPath);
        Manro::CRresMount::MountArchive(*m_Vfs, rresPath);
    }

    m_InputManager.SetBackend(&m_InputBackend);
    m_BenchFrameTimes.reserve(30 * 500);

    m_Physics = Manro::CreateScope<Manro::CPhysicsWorld>();

    LoadScene();
    BuildWorldColliders();

    {
        Manro::CPhysicsWorld::DynamicBodyDesc_t desc;
        desc.mass = 80.f;
        desc.allowSleeping = false;
        desc.lockRotation = true;
        m_PlayerBody = m_Physics->AddDynamicBox(m_Camera.Position, kPlayerHalfExtents, desc);
    }
}

void CSponza::OnShutdown() {
    if (m_Physics) {
        if (m_PlayerBody != Manro::kInvalidBodyHandle)
            m_Physics->RemoveBody(m_PlayerBody);
        for (auto h: m_StaticWorldBodies)
            if (h != Manro::kInvalidBodyHandle) m_Physics->RemoveBody(h);
        m_StaticWorldBodies.clear();
        m_Physics.reset();
    }
    m_MapModels.clear();
}

void CSponza::BuildWorldColliders() {
    if (!m_Physics) return;
    for (const auto &e: m_Map.Entities()) {
        if (e.collider.shape != Manro::ColliderShape_e::Box) continue;
        const Manro::Vec3 worldPos = e.position + e.collider.offset * e.scale;
        const Manro::Vec3 he = e.collider.halfExtents * e.scale;
        auto h = m_Physics->AddStaticBox(worldPos, he);
        if (h != Manro::kInvalidBodyHandle) m_StaticWorldBodies.push_back(h);
    }
    for (const auto &c: m_Map.Colliders()) {
        if (c.shape != Manro::ColliderShape_e::Box) continue;
        auto h = m_Physics->AddStaticBox(c.position, c.halfExtents);
        if (h != Manro::kInvalidBodyHandle) m_StaticWorldBodies.push_back(h);
    }
    LOG_INFO("[CSponza] built {} static world colliders", m_StaticWorldBodies.size());
}

void CSponza::StepPlayer(float dt) {
    if (!m_Physics || m_PlayerBody == Manro::kInvalidBodyHandle) return;

    using K = Manro::Key;
    auto [mx, my] = m_InputManager.ConsumeMouseDelta();
    m_Camera.Yaw += mx * m_Camera.MouseSensitivity;
    m_Camera.Pitch = std::clamp(m_Camera.Pitch - my * m_Camera.MouseSensitivity, -89.f, 89.f);

    const Manro::Vec3 fwdFull = m_Camera.Forward();
    const Manro::Vec3 fwdFlat = glm::normalize(Manro::Vec3{fwdFull.x, 0.f, fwdFull.z});
    const Manro::Vec3 right = glm::normalize(glm::cross(fwdFlat, Manro::Vec3{0, 1, 0}));

    Manro::Vec3 wish{0.f};
    if (m_InputManager.IsKeyDown(K::W)) wish += fwdFlat;
    if (m_InputManager.IsKeyDown(K::S)) wish -= fwdFlat;
    if (m_InputManager.IsKeyDown(K::D)) wish += right;
    if (m_InputManager.IsKeyDown(K::A)) wish -= right;
    if (glm::length(wish) > 0.001f) wish = glm::normalize(wish);

    const float speed = m_InputManager.IsKeyDown(K::LeftShift) ? kRunSpeed : kWalkSpeed;
    m_PlayerVelocity.x = wish.x * speed;
    m_PlayerVelocity.z = wish.z * speed;

    const bool grounded = m_Physics->IsGrounded(m_PlayerBody);
    if (grounded && m_PlayerVelocity.y < 0.f) m_PlayerVelocity.y = 0.f;
    if (grounded && m_InputManager.IsKeyDown(K::Space)) m_PlayerVelocity.y = kJumpSpeed;
    if (!grounded) m_PlayerVelocity.y -= kGravity * dt;

    m_Physics->SetLinearVelocity(m_PlayerBody, m_PlayerVelocity);
    m_Physics->Step(dt);

    const Manro::Vec3 bodyPos = m_Physics->GetBodyPosition(m_PlayerBody);
    m_Camera.Position = bodyPos + Manro::Vec3{0.f, kEyeOffsetY, 0.f};
}

bool CSponza::OnUpdate(const Manro::FrameContext_t &ctx, const Manro::UserCmd_t & /*cmd*/) {
    if (!m_bIsRunning) return false;

    const float dt = ctx.DeltaTime;
    m_flAccumulatedTime += dt;

    if (s_AnimateSun) {
        s_TimeOfDay += dt * s_DaySpeed;
        if (s_TimeOfDay >= 24.f) s_TimeOfDay -= 24.f;
    }

    const bool ctrlDown = m_InputManager.IsKeyDown(Manro::Key::LeftCtrl);
    const bool f1Down = m_InputManager.IsKeyDown(Manro::Key::F1);
    const bool f11Down = m_InputManager.IsKeyDown(Manro::Key::F11);

    if (ctrlDown && !m_bCtrlWasDown) m_bInputCaptured = !m_bInputCaptured;
    if (f1Down && !m_bF1WasDown) {
        m_Renderer->SetDebugUIEnabled(!m_Renderer->IsDebugUIEnabled());
    }
    if (f11Down && !m_bF11WasDown) {
        m_Window->ToggleFullscreen();
        m_bF11WasDown = !m_bF11WasDown;
    }
    m_bCtrlWasDown = ctrlDown;
    m_bF1WasDown = f1Down;
    m_bF11WasDown = f11Down;

    if (m_InputManager.IsKeyDown(Manro::Key::Escape)) {
        if (m_BenchState == BenchmarkState::Running ||
            m_BenchState == BenchmarkState::Warmup) {
            m_BenchState = BenchmarkState::Idle;
            m_BenchFrameTimes.clear();
            m_Camera.Position = m_SavedCamPos;
            m_Camera.Yaw = m_flSavedCamYaw;
            m_Camera.Pitch = m_flSavedCamPitch;
            if (m_Physics && m_PlayerBody != Manro::kInvalidBodyHandle) {
                m_Physics->SetBodyPosition(m_PlayerBody,
                                           m_Camera.Position - Manro::Vec3{0.f, kEyeOffsetY, 0.f});
                m_PlayerVelocity = {0.f, 0.f, 0.f};
            }
            m_bInputCaptured = true;
        } else {
            return false;
        }
    }

    const bool benchActive = (m_BenchState == BenchmarkState::Warmup ||
                              m_BenchState == BenchmarkState::Running);
    if (benchActive) {
        AdvanceBenchCamera(dt);
    } else if (m_bInputCaptured) {
        StepPlayer(dt);
    } else {
        m_InputManager.ConsumeMouseDelta();
        if (m_Physics) m_Physics->Step(dt);
    }

    return true;
}

void CSponza::OnRender(Manro::FrameContext_t &frame) {
    const float dt = frame.DeltaTime;

    m_Window->CaptureMouse(m_bInputCaptured);
    m_Window->ShowCursor(!m_bInputCaptured);

    const Manro::Mat4 view = m_Camera.View();
    const Manro::Mat4 proj = FlyCamera_t::Projection(kFov, m_Renderer->GetAspectRatio(), kNearZ, kFarZ);

    m_Renderer->SetViewProjection(view, proj);
    m_Renderer->SetCameraPosition(m_Camera.Position);

    m_Renderer->ClearLights();

    const float dayTau = (s_TimeOfDay / 24.f) * 2.f * 3.14159265f;
    const float sunAlt = sinf(dayTau - 1.5707f);
    const float sunAzimuth = cosf(dayTau - 1.5707f);

    Manro::LightData sun{};
    sun.type = shaderio::eLightTypeDirectional;
    sun.direction = glm::normalize(Manro::Vec3{sunAzimuth, -sunAlt, 0.3f});

    if (sunAlt > 0.05f) {
        sun.color = {1.0f, 0.98f, 0.90f};
        sun.intensity = 3.f * sunAlt;
    } else if (sunAlt > -0.1f) {
        const float t = (sunAlt + 0.1f) / 0.15f;
        sun.color = glm::mix(Manro::Vec3{1.f, 0.3f, 0.05f},
                             Manro::Vec3{1.f, 0.98f, 0.90f}, t);
        sun.intensity = 0.8f;
    } else {
        sun.direction = glm::normalize(Manro::Vec3{-sunAzimuth, sunAlt, -0.3f});
        sun.color = {0.1f, 0.15f, 0.35f};
        sun.intensity = 0.2f;
    }
    m_Renderer->AddLight(sun);

    for (const auto &l: m_Map.Lights()) {
        Manro::LightData ld{};
        ld.type = (l.type == 0)
                      ? shaderio::eLightTypeDirectional
                      : shaderio::eLightTypePoint;
        ld.position = l.position;
        ld.direction = glm::normalize(l.direction);
        ld.color = l.color;
        ld.intensity = l.intensity;
        if (l.type != 0)
            ld.angularSizeOrInvRange = 1.f / std::max(l.range, 0.001f);
        m_Renderer->AddLight(ld);
    }

    const bool benchActive = (m_BenchState == BenchmarkState::Warmup ||
                              m_BenchState == BenchmarkState::Running);
    if (benchActive) {
        for (const auto &l: m_BenchLights) m_Renderer->AddLight(l);
        TickBenchmark(dt);
    }

    m_Renderer->BeginRendering();
    m_Renderer->RenderQueue();

    DrawGui(dt);
    m_Renderer->EndRendering();

    m_LastStats = m_Renderer->GetLastFrameStats();
    const float frameMs = dt * 1000.f;
    m_flFrameTimeHistory[m_nFrameTimeOffset] = frameMs;
    m_nFrameTimeOffset = (m_nFrameTimeOffset + 1) % kHistoryLen;
}

void CSponza::LoadScene() {
    bool gotMap = false;
    const std::string mmapPath = m_Vfs->ResolvePath("scenes/test.mmap");
    if (std::filesystem::exists(mmapPath))
        gotMap = m_Map.LoadFromFile(mmapPath);

    if (gotMap && !m_Map.Entities().empty()) {
        LOG_INFO("[CSponza] Loaded map: {} entities, {} lights",
                 m_Map.Entities().size(), m_Map.Lights().size());
        for (const auto &e: m_Map.Entities()) {
            if (e.modelPath.empty()) continue;
            if (m_MapModels.contains(e.modelPath)) continue;
            auto loaded = Manro::CModel::Load({e.modelPath}, *m_Renderer, *m_Jobs, *m_Vfs);
            if (!loaded.empty() && loaded[0])
                m_MapModels.emplace(e.modelPath, std::move(loaded[0]));
            else
                LOG_ERROR("[CSponza] Map references missing model '{}'", e.modelPath);
        }
        for (const auto &e: m_Map.Entities()) {
            auto it = m_MapModels.find(e.modelPath);
            if (it == m_MapModels.end() || !it->second) continue;
            m_Renderer->DrawModelStatic(*it->second,
                                        Manro::CMap::ComposeEntityTransform(e));
        }
    }

    auto skyFaces = Manro::CTextureLoader::LoadCubemap("skyboxes/cubemap_sky.png", *m_Vfs);
    if (!skyFaces.empty()) {
        auto h = m_Renderer->UploadCubemap(skyFaces);
        m_Renderer->SetSkybox(h);
    }
}


void CSponza::DrawGui(const float dt) {
    const float fps = dt > 0.f ? 1.f / dt : 0.f;
    const bool benchActive = (m_BenchState == BenchmarkState::Warmup ||
                              m_BenchState == BenchmarkState::Running);

    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);

    if (ImGui::Begin("Benchmark Menu")) {
        bool showProfiler = m_Renderer->IsDebugUIEnabled();
        if (ImGui::Checkbox("Engine profiler overlay", &showProfiler)) {
            m_Renderer->SetDebugUIEnabled(showProfiler);
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Scene Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("FoV", &kFov, 50.f, 120.f);
            ImGui::Checkbox("Animate Day Cycle", &s_AnimateSun);
            ImGui::SliderFloat("Time", &s_TimeOfDay, 0.f, 24.f, "%.1f h");
            ImGui::SliderFloat("Day Speed", &s_DaySpeed, 0.f, 5.f);
        }
        ImGui::Separator();

        if (benchActive) {
            if (m_BenchState == BenchmarkState::Warmup) {
                const float prog = m_flWarmupElapsed / m_flWarmupDuration;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.8f, 0.6f, 0.1f, 1.f});
                ImGui::ProgressBar(prog, {-FLT_MIN, 0}, "Warming up...");
                ImGui::PopStyleColor();
                ImGui::Text("%.1f / %.0fs", m_flWarmupElapsed, m_flWarmupDuration);
            } else {
                const float prog = m_flBenchElapsed / static_cast<float>(m_nBenchDuration);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.2f, 0.8f, 0.3f, 1.f});
                ImGui::ProgressBar(prog, {-FLT_MIN, 0}, "Benchmarking...");
                ImGui::PopStyleColor();
                ImGui::Text("%.1f / %ds  |  %u frames  |  %.1f fps",
                            m_flBenchElapsed, m_nBenchDuration,
                            static_cast<Manro::u32>(m_BenchFrameTimes.size()), fps);
            }
            if (ImGui::Button("Cancel Benchmark", {-FLT_MIN, 0})) {
                m_BenchState = BenchmarkState::Idle;
                m_BenchFrameTimes.clear();
                m_Camera.Position = m_SavedCamPos;
                m_Camera.Yaw = m_flSavedCamYaw;
                m_Camera.Pitch = m_flSavedCamPitch;
                if (m_Physics && m_PlayerBody != Manro::kInvalidBodyHandle) {
                    m_Physics->SetBodyPosition(m_PlayerBody,
                                               m_Camera.Position - Manro::Vec3{0.f, kEyeOffsetY, 0.f});
                    m_PlayerVelocity = {0.f, 0.f, 0.f};
                }
                m_bInputCaptured = true;
            }
            ImGui::TextDisabled("Press Escape to cancel");
        } else {
            if (ImGui::Button("Run Benchmark", {-FLT_MIN, 0})) {
                StartBenchmark();
            }
            if (ImGui::Button("Open Benchmark Details", {-FLT_MIN, 0})) {
                m_bShowBenchWindow = true;
            }
            if (m_BenchState == BenchmarkState::Done)
                ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f},
                                   "%.1f avg FPS  |  %.2f ms avg",
                                   m_BenchResult.avgFps, m_BenchResult.avgFrameMs);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("F1 toggle engine overlay  |  Left Ctrl toggle cursor");
    }
    ImGui::End();

    if (!m_bShowBenchWindow) return;

    ImGui::SetNextWindowPos({350, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({460, 0}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Benchmark Details", &m_bShowBenchWindow)) {
        ImGui::End();
        return;
    }

    if (m_BenchState == BenchmarkState::Idle || m_BenchState == BenchmarkState::Done) {
        ImGui::SeparatorText("Settings");
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Duration (s)", &m_nBenchDuration, 5, 120);
        ImGui::SameLine();
        ImGui::TextDisabled("(+%.0fs warmup)", m_flWarmupDuration);
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Random lights", &m_nBenchLightCount, 0, 64);
        ImGui::Spacing();
        ImGui::TextDisabled("Camera follows a pre-defined tour of the CSponza atrium.");
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
        const float prog = m_flWarmupElapsed / m_flWarmupDuration;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.8f, 0.6f, 0.1f, 1.f});
        ImGui::ProgressBar(prog, {-FLT_MIN, 24});
        ImGui::PopStyleColor();
        ImGui::Text("Letting GPU settle  %.1f / %.0f s", m_flWarmupElapsed, m_flWarmupDuration);
    }

    if (m_BenchState == BenchmarkState::Running) {
        ImGui::SeparatorText("Running");
        const float prog = m_flBenchElapsed / static_cast<float>(m_nBenchDuration);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.2f, 0.8f, 0.3f, 1.f});
        ImGui::ProgressBar(prog, {-FLT_MIN, 24});
        ImGui::PopStyleColor();
        if (!m_BenchFrameTimes.empty()) {
            ImGui::Text("FPS %.1f  |  frame %u  |  %.1fs / %ds",
                        1000.f / m_BenchFrameTimes.back(),
                        static_cast<Manro::u32>(m_BenchFrameTimes.size()),
                        m_flBenchElapsed, m_nBenchDuration);
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
        const auto &r = m_BenchResult;
        ImGui::PushStyleColor(ImGuiCol_Text, {0.4f, 1.f, 0.4f, 1.f});
        ImGui::Text("Avg %.2f FPS    Min %.2f    Max %.2f", r.avgFps, r.minFps, r.maxFps);
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
#define ROW(label, fmt, ...) snprintf(buf, sizeof(buf), fmt, __VA_ARGS__); row(label, buf)
            ROW("Avg frame time", "%.3f ms", r.avgFrameMs);
            ROW("1%% low (99th)", "%.3f ms", r.p1FrameMs);
            ROW("0.1%% low (99.9th)", "%.3f ms", r.p01FrameMs);
            ROW("Total frames", "%u", r.totalFrames);
            ROW("Total time", "%.2f s", r.totalSeconds);
            ROW("Avg draw calls", "%u", r.avgDrawCalls);
            ROW("Avg triangles", "%u", r.avgTriangles);
            ROW("Random lights", "%d", m_nBenchLightCount);
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
                     "Scene: CSponza  |  Duration: %.2fs  |  Frames: %u\n"
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

void CSponza::StartBenchmark() {
    m_bShowBenchWindow = true;
    m_SavedCamPos = m_Camera.Position;
    m_flSavedCamYaw = m_Camera.Yaw;
    m_flSavedCamPitch = m_Camera.Pitch;

    m_flPathT = 0.f;
    m_flPathSpeed = static_cast<float>(kWaypointCount - 1) /
                    static_cast<float>(m_nBenchDuration);

    m_BenchState = BenchmarkState::Warmup;
    m_flBenchElapsed = 0.f;
    m_flWarmupElapsed = 0.f;
    m_BenchDrawCallsAcc = 0;
    m_BenchTrianglesAcc = 0;
    m_BenchResult = {};
    m_BenchFrameTimes.clear();
    m_BenchFrameTimes.reserve(static_cast<size_t>(m_nBenchDuration) * 500);
    m_bInputCaptured = false;
    m_InputManager.ConsumeMouseDelta();

    m_BenchLights.clear();
    std::mt19937 rng(0xBEEF1234);
    std::uniform_real_distribution rx(-1200.f, 1200.f);
    std::uniform_real_distribution ry(50.f, 400.f);
    std::uniform_real_distribution rz(-500.f, 500.f);
    std::uniform_real_distribution rc(0.3f, 1.f);
    for (int i = 0; i < m_nBenchLightCount; ++i) {
        Manro::LightData l{};
        l.type = shaderio::eLightTypePoint;
        l.position = {rx(rng), ry(rng), rz(rng)};
        l.color = {rc(rng), rc(rng), rc(rng)};
        l.intensity = 800.f;
        l.angularSizeOrInvRange = 1.f / 350.f;
        m_BenchLights.push_back(l);
    }
    LOG_INFO("[Benchmark] Warmup {}s then running {}s...",
             static_cast<int>(m_flWarmupDuration), m_nBenchDuration);
}

void CSponza::TickBenchmark(float dt) {
    if (m_BenchState == BenchmarkState::Warmup) {
        m_flWarmupElapsed += dt;
        if (m_flWarmupElapsed >= m_flWarmupDuration) {
            m_BenchState = BenchmarkState::Running;
            m_flBenchElapsed = 0.f;
            LOG_INFO("[Benchmark] Warmup done, measuring...");
        }
        return;
    }

    m_BenchFrameTimes.push_back(dt * 1000.f);
    m_BenchDrawCallsAcc += m_LastStats.drawCalls;
    m_BenchTrianglesAcc += m_LastStats.triangleCount;
    m_flBenchElapsed += dt;

    if (m_flBenchElapsed >= static_cast<float>(m_nBenchDuration))
        FinishBenchmark();
}

void CSponza::FinishBenchmark() {
    auto &r = m_BenchResult;
    r.totalFrames = static_cast<Manro::u32>(m_BenchFrameTimes.size());
    r.totalSeconds = m_flBenchElapsed;
    r.avgDrawCalls = static_cast<Manro::u32>(
        m_BenchDrawCallsAcc / std::max(r.totalFrames, 1u));
    r.avgTriangles = static_cast<Manro::u32>(
        m_BenchTrianglesAcc / std::max(r.totalFrames, 1u));

    const float sum = std::accumulate(
        m_BenchFrameTimes.begin(), m_BenchFrameTimes.end(), 0.f);
    r.avgFrameMs = sum / static_cast<float>(r.totalFrames);
    r.avgFps = 1000.f / r.avgFrameMs;

    const float minMs = *std::ranges::min_element(m_BenchFrameTimes);
    const float maxMs = *std::ranges::max_element(m_BenchFrameTimes);
    r.minFps = 1000.f / maxMs;
    r.maxFps = 1000.f / minMs;

    std::vector<float> sorted = m_BenchFrameTimes;
    std::ranges::sort(sorted);
    auto pct = [&](float p) -> float {
        const auto i = static_cast<size_t>(p * static_cast<float>(sorted.size() - 1));
        return sorted[std::min(i, sorted.size() - 1)];
    };
    r.p1FrameMs = pct(0.99f);
    r.p01FrameMs = pct(0.999f);

    m_BenchState = BenchmarkState::Done;
    m_Camera.Position = m_SavedCamPos;
    m_Camera.Yaw = m_flSavedCamYaw;
    m_Camera.Pitch = m_flSavedCamPitch;
    if (m_Physics && m_PlayerBody != Manro::kInvalidBodyHandle) {
        m_Physics->SetBodyPosition(m_PlayerBody,
                                   m_Camera.Position - Manro::Vec3{0.f, kEyeOffsetY, 0.f});
        m_PlayerVelocity = {0.f, 0.f, 0.f};
    }
    m_bInputCaptured = true;

    LOG_INFO("[Benchmark] Done  {:.2f} avg FPS  |  {:.3f}ms avg  |"
             "  {:.3f}ms 1% low  |  {:.3f}ms 0.1% low",
             r.avgFps, r.avgFrameMs, r.p1FrameMs, r.p01FrameMs);
}

void CSponza::AdvanceBenchCamera(float dt) {
    m_flPathT += m_flPathSpeed * dt;
    const int loopCount = kWaypointCount - 1;
    while (m_flPathT >= static_cast<float>(loopCount))
        m_flPathT -= static_cast<float>(loopCount);

    const int seg = static_cast<int>(m_flPathT);
    const float t = m_flPathT - static_cast<float>(seg);

    auto wp = [&](int i) -> const BenchWaypoint_t & {
        i = std::max(0, std::min(kWaypointCount - 1, i));
        return kWaypoints[i];
    };

    m_Camera.Position = CatmullRomPos(wp(seg - 1), wp(seg), wp(seg + 1), wp(seg + 2), t);
    m_Camera.Yaw = CatmullRomAngle(wp(seg - 1).yaw, wp(seg).yaw,
                                   wp(seg + 1).yaw, wp(seg + 2).yaw, t);
    m_Camera.Pitch = CatmullRomAngle(wp(seg - 1).pitch, wp(seg).pitch,
                                     wp(seg + 1).pitch, wp(seg + 2).pitch, t);
}

Manro::Vec3 CSponza::CatmullRomPos(const BenchWaypoint_t &p0, const BenchWaypoint_t &p1,
                                   const BenchWaypoint_t &p2, const BenchWaypoint_t &p3,
                                   float t) {
    const float t2 = t * t, t3 = t2 * t;
    const Manro::Vec3 a = -0.5f * p0.position + 1.5f * p1.position - 1.5f * p2.position + 0.5f * p3.position;
    const Manro::Vec3 b = p0.position - 2.5f * p1.position + 2.f * p2.position - 0.5f * p3.position;
    const Manro::Vec3 c = -0.5f * p0.position + 0.5f * p2.position;
    return a * t3 + b * t2 + c * t + p1.position;
}

float CSponza::CatmullRomAngle(float a0, float a1, float a2, float a3, float t) {
    auto unwrap = [](float base, float angle) {
        while (angle - base > 180.f) angle -= 360.f;
        while (angle - base < -180.f) angle += 360.f;
        return angle;
    };
    a2 = unwrap(a1, a2);
    a3 = unwrap(a2, a3);
    a0 = unwrap(a1, a0);
    const float t2 = t * t, t3 = t2 * t;
    return (-0.5f * a0 + 1.5f * a1 - 1.5f * a2 + 0.5f * a3) * t3
           + (a0 - 2.5f * a1 + 2.f * a2 - 0.5f * a3) * t2
           + (-0.5f * a0 + 0.5f * a2) * t
           + a1;
}

Manro::Vec3 FlyCamera_t::Forward() const {
    const float yR = glm::radians(Yaw), pR = glm::radians(Pitch);
    return glm::normalize(Manro::Vec3{cosf(pR) * cosf(yR), sinf(pR), cosf(pR) * sinf(yR)});
}

void FlyCamera_t::Update(const Manro::CInputManager &input, float dt) {
    using K = Manro::Key;
    auto [x, y] = const_cast<Manro::CInputManager &>(input).ConsumeMouseDelta();
    Yaw += x * MouseSensitivity;
    Pitch = std::clamp(Pitch - y * MouseSensitivity, -89.f, 89.f);

    const Manro::Vec3 fwd = Forward();
    const Manro::Vec3 right = glm::normalize(glm::cross(fwd, Manro::Vec3{0, 1, 0}));
    constexpr Manro::Vec3 up = {0, 1, 0};
    const float speed = input.IsKeyDown(K::LeftShift) ? SprintSpeed : NormalSpeed;

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

Manro::Mat4 FlyCamera_t::View() const {
    return glm::lookAt(Position, Position + Forward(), {0, 1, 0});
}

Manro::Mat4 FlyCamera_t::Projection(float fovDeg, float aspect, float nearZ, float farZ) {
    return glm::perspective(glm::radians(fovDeg), aspect, nearZ, farZ);
}

const BenchWaypoint_t CSponza::kWaypoints[] = {
    {{-1200.f, 150.f, 0.f}, -90.f, -8.f},
    {{-700.f, 150.f, 0.f}, -90.f, -5.f},
    {{-200.f, 200.f, 40.f}, -60.f, -15.f},
    {{0.f, 350.f, 0.f}, -90.f, -35.f},
    {{200.f, 200.f, -40.f}, -120.f, -15.f},
    {{700.f, 150.f, 0.f}, -90.f, -5.f},
    {{1200.f, 150.f, 0.f}, -90.f, -8.f},
    {{900.f, 120.f, 300.f}, -160.f, -5.f},
    {{0.f, 120.f, 500.f}, 180.f, -8.f},
    {{-900.f, 120.f, 300.f}, 160.f, -5.f},
    {{-1000.f, 400.f, 0.f}, -70.f, -20.f},
    {{0.f, 500.f, 0.f}, -90.f, -50.f},
    {{1000.f, 400.f, 0.f}, -110.f, -20.f},
    {{600.f, 80.f, -400.f}, 10.f, -3.f},
    {{0.f, 80.f, -600.f}, 0.f, -3.f},
    {{-600.f, 80.f, -400.f}, -10.f, -3.f},
    {{-1200.f, 150.f, 0.f}, -90.f, -8.f},
};
const int CSponza::kWaypointCount = 17;
