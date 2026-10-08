#pragma once

#include "Player.h"

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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>

// CVoxel: playable block-world sample on the GPU-driven voxel path.
// - First-person player (walk/sprint/jump/swim, noclip fly on F) with AABB
//   collision against the streamed world; chunks stream + evict around the
//   player with no preallocated volume limit.
// - LMB breaks (raycast), RMB places, F11 toggles fullscreen, Esc frees the
//   mouse (click re-grabs, Esc again quits). B/N are culling kill switches,
//   V toggles vsync.
class CVoxel final : public Manro::IApplication {
public:
    // Startup parameters (parsed from argv in main.cpp). No env vars.
    struct Params {
        std::string worldDir; // Anvil save dir (required; no fallback)
        std::string assetsDir; // empty = build-time client-jar assets
        int radius{6}; // section radius, clamped to [1, 10]
        float resScale{1.f}; // offscreen resolution scale, clamped to [0.25, 1]
        bool debug{false}; // GPU debug counters (costs a readback)
        bool chaos{false}; // streaming-torture teleports
    };

    explicit CVoxel(Params params);

    Manro::WindowDesc_t GetWindowDesc() const override {
        Manro::WindowDesc_t d;
        d.Title = "Voxel";
        d.Width = 1280;
        d.Height = 720;
        d.Fullscreen = true;
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
            // Fill-bound probe: --res-scale=0.5 renders the offscreen at
            // half res (composite upscales to the swapchain).
            if (m_Params.resScale < 1.f) {
                s.resolutionScale = m_Params.resScale;
                printf("[Voxel] resolutionScale=%.2f\n", static_cast<double>(m_Params.resScale));
            }
            m_Renderer->SetSettings(s);
        }

        // Voxel path first: sparse world + PSOs.
        m_Renderer->VoxelInit(1280, 720);

        // Streamed path: block tiles from the build-time client-jar assets
        // (--assets-dir overrides the default) plus the Anvil save under
        // --world-dir (required — without a save the volume fills as air).
        m_Spawn = m_Renderer->VoxelStreamInit(m_Params.worldDir.c_str(), m_Params.assetsDir.c_str(),
                                              m_Params.radius);
        printf("[Voxel] stream spawn=(%.1f,%.1f,%.1f) world=%s\n", m_Spawn.x, m_Spawn.y, m_Spawn.z,
               m_Params.worldDir.empty() ? "<no-save>" : m_Params.worldDir.c_str());

        // Default placement state (planks) until the first MMB pick.
        m_PlaceState = m_Renderer->VoxelStreamPlaceState();
        m_PlaceKey = m_Renderer->VoxelGetStateKey(m_PlaceState);
        // Player starts at spawn (falls to the ground once it streams in);
        // camera is the eye, driven by mouse look below.
        m_Player.pos = m_Spawn;
        m_CamPos = m_Player.pos + Manro::Vec3(0.f, CPlayer::kEye, 0.f);
        m_Fwd = Manro::Vec3(0, 0, -1);
        m_Yaw = -90.f;
        m_Pitch = -10.f;

        // Captured mouse for first-person look (Esc releases, click re-grabs).
        m_Window->CaptureMouse(true);
        m_Window->ShowCursor(false);
        m_bGrabbed = true;

        // GPU debug counters cost a vkQueueWaitIdle readback on capture
        // frames: opt-in via --debug, off by default so the steady
        // log/HUD numbers measure the renderer, not the diagnostic stall.
        m_DbgAllowed = m_Params.debug;
        m_Chaos = m_Params.chaos;
    }

    void OnShutdown() override {
        if (m_Renderer)
            m_Renderer->VoxelShutdown();
    }

    bool OnUpdate(const Manro::FrameContext_t &ctx, const Manro::UserCmd_t &) override {
        using K = Manro::Key;
        using MB = Manro::MouseButton;

        // Escape: release the mouse first, quit when already released.
        if (m_InputManager.IsKeyDown(K::Escape) && !m_bEscHeld) {
            m_bEscHeld = true;
            if (m_bGrabbed) {
                m_Window->CaptureMouse(false);
                m_Window->ShowCursor(true);
                m_bGrabbed = false;
            } else {
                return false;
            }
        } else if (!m_InputManager.IsKeyDown(K::Escape)) {
            m_bEscHeld = false;
        }

        // Click re-grabs a released cursor (that click never edits).
        bool justGrabbed = false;
        if (!m_bGrabbed && m_InputManager.IsMouseButtonDown(MB::Left)) {
            m_Window->CaptureMouse(true);
            m_Window->ShowCursor(false);
            m_bGrabbed = true;
            justGrabbed = true;
        }

        // Fullscreen toggle.
        if (m_InputManager.IsKeyDown(K::F11) && !m_bF11Held) {
            m_bF11Held = true;
            m_Window->ToggleFullscreen();
        } else if (!m_InputManager.IsKeyDown(K::F11)) {
            m_bF11Held = false;
        }

        // Mouse look (drained always so no jump on re-grab).
        auto [mx, my] = m_InputManager.ConsumeMouseDelta();
        if (m_bGrabbed) {
            m_Yaw += mx * 0.1f;
            m_Pitch = std::clamp(m_Pitch - my * 0.1f, -89.f, 89.f);
        }

        const float yr = glm::radians(m_Yaw), pr = glm::radians(m_Pitch);
        Manro::Vec3 fwd{cosf(pr) * cosf(yr), sinf(pr), cosf(pr) * sinf(yr)};
        fwd = glm::normalize(fwd);
        Manro::Vec3 yawFwd{cosf(yr), 0.f, sinf(yr)};
        yawFwd = glm::normalize(yawFwd);
        Manro::Vec3 right = glm::normalize(glm::cross(fwd, Manro::Vec3{0, 1, 0}));
        Manro::Vec3 yawRight = glm::normalize(glm::cross(yawFwd, Manro::Vec3{0, 1, 0}));
        m_Fwd = fwd;

        // Player physics (walk/swim/fly, collision, gravity substeps).
        PlayerUpdate(m_Player, *m_Renderer, m_InputManager, yawFwd, yawRight, fwd, ctx.DeltaTime,
                     ctx.TotalTime);

        // Streaming-torture teleports (repro for the streaming-bug
        // bisection): opt-in via --chaos, OFF by default — when on,
        // the world perpetually restreams and most of the view is unfilled
        // holes while it catches up.
        if (m_Chaos) {
            if (m_Frame == 700)
                m_Player.pos += Manro::Vec3(80.f, 0.f, 0.f);
            if (m_Frame == 1400)
                m_Player.pos += Manro::Vec3(0.f, 0.f, -80.f);
            if (m_Frame == 2100)
                m_Player.pos -= Manro::Vec3(80.f, 0.f, 80.f);
        }

        // Fell out of the world (broken save edge): back to spawn.
        if (m_Player.pos.y < -80.f) {
            m_Player.pos = m_Spawn;
            m_Player.vel = Manro::Vec3(0.f);
        }
        m_CamPos = m_Player.pos + Manro::Vec3(0.f, CPlayer::kEye, 0.f);

        // Block edit: LMB breaks, RMB places (raycast from the eye).
        m_BreakCd -= ctx.DeltaTime;
        const Manro::Vec3 eye = m_CamPos;
        if (m_bGrabbed && !justGrabbed && m_InputManager.IsMouseButtonDown(MB::Left) &&
            (!m_bLMBHeld || m_BreakCd <= 0.f)) {
            m_bLMBHeld = true;
            const auto hit = PlayerDetail::RaycastVoxel(*m_Renderer, eye, fwd, 8.f);
            if (hit.hit) {
                m_Renderer->VoxelQueueEdit(
                    Manro::Vec3(static_cast<float>(hit.hx) + 0.5f,
                                static_cast<float>(hit.hy) + 0.5f,
                                static_cast<float>(hit.hz) + 0.5f),
                    0.f, 0, 0);
                ++m_EditCount;
                m_BreakCd = 0.25f;
            }
        } else if (!m_InputManager.IsMouseButtonDown(MB::Left)) {
            m_bLMBHeld = false;
        }
        if (m_bGrabbed && !justGrabbed && m_InputManager.IsMouseButtonDown(MB::Right) &&
            !m_bRMBHeld) {
            m_bRMBHeld = true;
            const auto hit = PlayerDetail::RaycastVoxel(*m_Renderer, eye, fwd, 8.f);
            if (hit.hit) {
                const Manro::Vec3 target(static_cast<float>(hit.hx + hit.nx),
                                         static_cast<float>(hit.hy + hit.ny),
                                         static_cast<float>(hit.hz + hit.nz));
                if (!m_Renderer->VoxelStreamIsSolid(target)) {
                    // State-aware placement: orient the picked state from the
                    // view (facing/half/axis) instead of always dropping the
                    // same default variant. Unknown combos fall back inside.
                    const Manro::Vec3 hitPos = eye + fwd * hit.t;
                    const bool fluid = m_Renderer->VoxelStreamIsFluid(target);
                    const Manro::u32 state =
                        OrientPlaceState(m_PlaceState, hitPos.y, hit.nx, hit.ny, hit.nz, fluid);
                    m_Renderer->VoxelQueueEdit(target + Manro::Vec3(0.5f), 0.f, state, 1);
                    ++m_EditCount;
                }
            }
        } else if (!m_InputManager.IsMouseButtonDown(MB::Right)) {
            m_bRMBHeld = false;
        }
        // Pick-block: MMB copies the targeted voxel's exact state (variant
        // included) for RMB placement. Skip/air cells (flowers, torches)
        // aren't solid so the ray passes through them — not pickable.
        if (m_bGrabbed && !justGrabbed && m_InputManager.IsMouseButtonDown(MB::Middle) &&
            !m_bMMBHeld) {
            m_bMMBHeld = true;
            const auto hit = PlayerDetail::RaycastVoxel(*m_Renderer, eye, fwd, 8.f);
            if (hit.hit) {
                const Manro::i32 st = m_Renderer->VoxelStreamGetStateAt(
                    Manro::Vec3(static_cast<float>(hit.hx) + 0.5f,
                                static_cast<float>(hit.hy) + 0.5f,
                                static_cast<float>(hit.hz) + 0.5f));
                if (st > 0) {
                    m_PlaceState = static_cast<Manro::u32>(st);
                    m_PlaceKey = m_Renderer->VoxelGetStateKey(m_PlaceState);
                    printf("[Voxel] pick state=%u %s\n", m_PlaceState, m_PlaceKey.c_str());
                } else {
                    printf("[Voxel] pick: no state (air/unloaded)\n");
                }
            }
        } else if (!m_InputManager.IsMouseButtonDown(MB::Middle)) {
            m_bMMBHeld = false;
        }

        // Culling-stage kill switches for bisection: B = mesh backface,
        // N = task frustum. Both default on.
        if (m_InputManager.IsKeyDown(K::B) && !m_bBHeld) {
            m_bBHeld = true;
            m_UseBackface = !m_UseBackface;
            m_Renderer->VoxelSetBackfaceEnabled(m_UseBackface);
            printf("[Voxel] backface %s\n", m_UseBackface ? "ON" : "OFF");
        } else if (!m_InputManager.IsKeyDown(K::B)) {
            m_bBHeld = false;
        }
        if (m_InputManager.IsKeyDown(K::N) && !m_bNHeld) {
            m_bNHeld = true;
            m_UseFrustum = !m_UseFrustum;
            m_Renderer->VoxelSetFrustumEnabled(m_UseFrustum);
            printf("[Voxel] frustum %s\n", m_UseFrustum ? "ON" : "OFF");
        } else if (!m_InputManager.IsKeyDown(K::N)) {
            m_bNHeld = false;
        }
        if (m_InputManager.IsKeyDown(K::V) && !m_bVHeld) {
            m_bVHeld = true;
            Manro::RenderSettings_t s = m_Renderer->GetSettings();
            s.enableVSync = !s.enableVSync;
            m_Renderer->SetSettings(s);
            printf("[Voxel] vsync %s\n", s.enableVSync ? "ON" : "OFF");
        } else if (!m_InputManager.IsKeyDown(K::V)) {
            m_bVHeld = false;
        }
        return true;
    }

    void OnRender(Manro::FrameContext_t &frame) override {
        const auto t0 = std::chrono::steady_clock::now();
        // Sprint FOV kick (90 -> 100, smoothed). Snap when close so the
        // projection stops micro-creeping (variable dt would otherwise keep
        // it drifting by sub-pixel amounts long after the sprint ends).
        const float fovTarget = m_Player.sprinting ? 100.f : 90.f;
        m_Fov += (fovTarget - m_Fov) * std::min(1.f, frame.DeltaTime * 8.f);
        if (std::abs(fovTarget - m_Fov) < 0.01f)
            m_Fov = fovTarget;
        const Manro::Mat4 view = glm::lookAt(m_CamPos, m_CamPos + m_Fwd, Manro::Vec3{0, 1, 0});
        const Manro::Mat4 proj =
            glm::perspective(glm::radians(m_Fov), m_Renderer->GetAspectRatio(), 0.1f, 10000.f);
        m_Renderer->SetViewProjection(view, proj);
        m_Renderer->SetCameraPosition(m_CamPos);

        // Section streaming follows the player eye.
        m_Unfilled = m_Renderer->VoxelStreamUpdate();
        const auto t1 = std::chrono::steady_clock::now();

        // DEBUG capture only on opted-in log frames: skips per-frame Fill,
        // shader atomics, extra barriers and the WaitIdle readback otherwise.
        // The fps printf below stays on every 120th frame regardless.
        const bool wantLog = (m_Frame % 120) == 0;
        m_Renderer->VoxelSetDebugEnabled(wantLog && m_DbgAllowed);

        m_Renderer->BeginRendering();
        m_Renderer->RenderQueue();
        m_Renderer->EndRendering();
        const auto t2 = std::chrono::steady_clock::now();
        // CPU share of the frame: streaming + command recording (t0->t2 is
        // CPU-only; the GPU works the submitted CB asynchronously, so
        // dt - cpuMs is queue/present/compositor + GPU time).
        const float streamMs =
            std::chrono::duration<float, std::milli>(t1 - t0).count();
        const float cpuMs =
            std::chrono::duration<float, std::milli>(t2 - t0).count();
        m_CpuEma = (m_CpuEma <= 0.f) ? cpuMs : m_CpuEma + (cpuMs - m_CpuEma) * 0.05f;
        m_StreamEma =
            (m_StreamEma <= 0.f) ? streamMs : m_StreamEma + (streamMs - m_StreamEma) * 0.05f;

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
            ImGui::Text("Bricks: %u  TaskGroups: %u  Edits: %u  Unfilled: %d",
                        m_Renderer->VoxelGetBrickCount(), m_Renderer->VoxelGetTaskGroups(),
                        m_EditCount, m_Unfilled);
            ImGui::Text("Player: (%.1f,%.1f,%.1f) %s%s", static_cast<double>(m_Player.pos.x),
                        static_cast<double>(m_Player.pos.y), static_cast<double>(m_Player.pos.z),
                        m_Player.fly ? "FLY"
                                     : (m_Player.sprinting ? "SPRINT"
                                                           : (m_Player.onGround ? "GROUND" : "AIR")),
                        m_bGrabbed ? "" : " (cursor free)");
            ImGui::TextDisabled("task=%u vis=%u faces=%u cull=%u mesh=%u mfaces=%u", dbg[0], dbg[1],
                                dbg[2], dbg[3], dbg[4], dbg[5]);
            ImGui::TextDisabled("backface=%d frustum=%d", m_UseBackface ? 1 : 0,
                                m_UseFrustum ? 1 : 0);
            ImGui::TextDisabled("placing: %s",
                               m_PlaceKey.empty() ? "?" : m_PlaceKey.c_str());
            ImGui::TextDisabled(
                "WASD move | 2xW/Shift/Ctrl sprint | Space jump | F fly | LMB break | RMB place");
            ImGui::TextDisabled("MMB pick block | F11 fullscreen | B/N cull | V vsync | Esc cursor/quit");
        }
        ImGui::End();

        const float fps = frame.DeltaTime > 0.f ? 1.f / frame.DeltaTime : 0.f;
        (void)fps;
        if (wantLog && m_Frame > 0) {
            // EMA, not the instantaneous sample: a single WaitIdle / upload
            // stall frame would otherwise dominate the reported number.
            printf("[Voxel] fps=%.0f (%.2f ms) 1%%low=%.2fms 0.1%%low=%.2fms bricks=%u "
                   "taskGroups=%u edits=%u unfilled=%d cpu=%.2fms stream=%.2fms cam=(%.1f,%.1f,%.1f)\n",
                   static_cast<double>(emaFps), static_cast<double>(m_FpsEma),
                   static_cast<double>(worst1), static_cast<double>(worst01),
                   m_Renderer->VoxelGetBrickCount(), m_Renderer->VoxelGetTaskGroups(), m_EditCount,
                   m_Unfilled, static_cast<double>(m_CpuEma), static_cast<double>(m_StreamEma),
                   m_CamPos.x, m_CamPos.y, m_CamPos.z);
            // DEBUG: [0]=taskRuns [1]=visible [2]=faces [3]=culled [4]=meshRuns [5]=meshFaces
            if (m_DbgAllowed)
                printf("[VoxelDbg] taskRuns=%u visible=%u faces=%u culled=%u meshRuns=%u "
                       "meshFaces=%u\n",
                       dbg[0], dbg[1], dbg[2], dbg[3], dbg[4], dbg[5]);
        }
        ++m_Frame;
    }

    Manro::CInputManager *GetInputManager() override { return &m_InputManager; }
  private:
    // Placement helpers (state-aware, cube-mesher constraints).
    static void SplitStateKey(const std::string &key, std::string &nameOut,
                              std::map<std::string, std::string> &propsOut) {
        const size_t bar = key.find('|');
        nameOut = (bar == std::string::npos) ? key : key.substr(0, bar);
        propsOut.clear();
        if (bar == std::string::npos)
            return;
        size_t i = bar + 1;
        while (i < key.size()) {
            const size_t eq = key.find('=', i);
            if (eq == std::string::npos)
                break;
            const size_t sc = key.find(';', eq + 1);
            if (sc == std::string::npos)
                break;
            propsOut[key.substr(i, eq - i)] = key.substr(eq + 1, sc - eq - 1);
            i = sc + 1;
        }
    }

    static std::string JoinStateKey(const std::string &name,
                                    const std::map<std::string, std::string> &props) {
        std::string key = name;
        key += '|';
        for (const auto &p : props) {
            key += p.first;
            key += '=';
            key += p.second;
            key += ';';
        }
        return key;
    }

    // Resolve the state to place for a picked base state: orient from the
    // player view (facing/half/axis), reset dynamic props (open=false),
    // set waterlogged from the target cell. Every substitution is
    // try-and-fall-back: unknown combinations keep the picked state, so a
    // partial rule set can never produce magenta/fallback blocks.
    Manro::u32 OrientPlaceState(Manro::u32 base, float hitY, int nx, int ny, int nz,
                               bool targetFluid) const {
        const std::string baseKey = m_Renderer->VoxelGetStateKey(base);
        if (baseKey.empty())
            return base;
        std::string name;
        std::map<std::string, std::string> props;
        SplitStateKey(baseKey, name, props);
        if (props.empty())
            return base;

        // Facing (trapdoor/stairs/furnace/...): block faces the player,
        // i.e. opposite the horizontal look direction. m_Yaw convention:
        // fwd = (cos yr, *, sin yr); north = -Z, south = +Z, east = +X.
        auto it = props.find("facing");
        if (it != props.end()) {
            const float fx = -m_Fwd.x, fz = -m_Fwd.z;
            const std::string want =
                (std::fabs(fx) > std::fabs(fz)) ? (fx > 0.f ? "east" : "west")
                                               : (fz > 0.f ? "south" : "north");
            if (it->second != want) {
                auto trial = props;
                trial["facing"] = want;
                const Manro::u32 id = m_Renderer->VoxelFindStateByKey(JoinStateKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        // Half / slab type from the hit: top/bottom faces decide directly,
        // side faces use the hit height within the target cell.
        // ny>0 = hit a top face (placing on top) -> bottom half;
        // ny<0 = hit a bottom face (placing underneath) -> top half.
        const bool wantTop =
            (ny > 0) ? false : (ny < 0) ? true : (hitY - std::floor(hitY) > 0.5f);
        it = props.find("half");
        if (it != props.end()) {
            const std::string want = wantTop ? "top" : "bottom";
            if (it->second != want) {
                auto trial = props;
                trial["half"] = want;
                const Manro::u32 id = m_Renderer->VoxelFindStateByKey(JoinStateKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        it = props.find("type");
        if (it != props.end() && (it->second == "top" || it->second == "bottom")) {
            const std::string want = wantTop ? "top" : "bottom";
            if (it->second != want) {
                auto trial = props;
                trial["type"] = want;
                const Manro::u32 id = m_Renderer->VoxelFindStateByKey(JoinStateKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        // Pillar axis from the face normal.
        it = props.find("axis");
        if (it != props.end()) {
            const std::string want = (ny != 0) ? "y" : (nx != 0 ? "x" : "z");
            if (it->second != want) {
                auto trial = props;
                trial["axis"] = want;
                const Manro::u32 id = m_Renderer->VoxelFindStateByKey(JoinStateKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        // Dynamic props reset to placed defaults.
        it = props.find("open");
        if (it != props.end() && it->second != "false") {
            auto trial = props;
            trial["open"] = "false";
            const Manro::u32 id = m_Renderer->VoxelFindStateByKey(JoinStateKey(name, trial));
            if (id != ~0u)
                props = std::move(trial);
        }
        it = props.find("waterlogged");
        if (it != props.end()) {
            const std::string want = targetFluid ? "true" : "false";
            if (it->second != want) {
                auto trial = props;
                trial["waterlogged"] = want;
                const Manro::u32 id = m_Renderer->VoxelFindStateByKey(JoinStateKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        const Manro::u32 resolved = m_Renderer->VoxelFindStateByKey(JoinStateKey(name, props));
        return (resolved != ~0u) ? resolved : base;
    }

    Params m_Params;
    Manro::CRenderer *m_Renderer{nullptr};
    Manro::CWindow *m_Window{nullptr};
    Manro::CInputBackend m_InputBackend;
    Manro::CInputManager m_InputManager;
    CPlayer m_Player;
    Manro::Vec3 m_Spawn{0.f};
    Manro::Vec3 m_CamPos{0.f};
    Manro::Vec3 m_Fwd{0, 0, -1};
    float m_Yaw{-90.f};
    float m_Pitch{-10.f};
    float m_Fov{90.f};
    Manro::u32 m_Frame{0};
    Manro::u32 m_EditCount{0};
    float m_BreakCd{0.f};
    bool m_bGrabbed{true};
    bool m_bLMBHeld{false};
    bool m_bRMBHeld{false};
    bool m_bMMBHeld{false};
    // State-aware placement: MMB pick-block copies the targeted voxel's
    // state id (exact variant), RMB places it oriented from the view.
    // Defaults to planks until the first pick.
    Manro::u32 m_PlaceState{1};
    std::string m_PlaceKey;
    bool m_bEscHeld{false};
    bool m_bF11Held{false};
    bool m_bBHeld{false};
    bool m_bNHeld{false};
    bool m_bVHeld{false};
    bool m_UseBackface{true};
    bool m_UseFrustum{true};
    bool m_DbgAllowed{false};
    bool m_Chaos{false};
    int m_Unfilled{0};

    // On-screen FPS state: EMA of frame dt + rolling window for lows.
    static constexpr Manro::u32 kFrameTimeWindow = 240;
    float m_FrameTimes[kFrameTimeWindow]{};
    Manro::u32 m_FrameTimeIdx{0};
    Manro::u32 m_FramesSeen{0};
    float m_FpsEma{0.f};
    float m_CpuEma{0.f};
    float m_StreamEma{0.f};
};

inline CVoxel::CVoxel(Params params) : m_Params(std::move(params)) {}
