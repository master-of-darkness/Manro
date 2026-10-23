#include "Voxel.h"

#include "AnvilSource.h"
#include "StateMap.h"

#include <Manro/Render/Renderer.h>
#include <Manro/Render/VoxelRuntime.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

CVoxel::CVoxel(Params params) : m_Params(std::move(params)) {
}

void CVoxel::OnStartup(const Manro::InitContext_t &ctx) {
    m_Renderer = &ctx.CRenderer;
    m_Window = &ctx.CWindow;
    m_InputManager.SetBackend(&m_InputBackend);
    m_Renderer->SetDebugUIEnabled(false);

    {
        Manro::RenderSettings_t s = m_Renderer->GetSettings();
        s.aaMode = Manro::AntiAliasingMode::None;
        s.msaaSamples = Manro::MSAASampleCount::MSAA_1X;
        if (m_Params.resScale < 1.f) {
            s.resolutionScale = m_Params.resScale;
            printf("[Voxel] resolutionScale=%.2f\n", static_cast<double>(m_Params.resScale));
        }
        m_Renderer->SetSettings(s);
    }

    m_VoxelRuntime = m_Renderer->GetVoxelRuntime();
    m_VoxelRuntime->Init(1280, 720);

    std::unique_ptr<Manro::IVoxelContentSource> content;
    if (!m_Params.worldDir.empty()) {
        auto anvil = std::make_unique<Manro::CAnvilContentSource>();
        if (anvil->Open(m_Params.worldDir))
            content = std::move(anvil);
        else
            printf("[Voxel] warning: failed to open world '%s'\n",
                   m_Params.worldDir.c_str());
    }
    m_Spawn = m_VoxelRuntime->StreamInit(std::move(content), m_Params.worldDir,
                                         m_Params.assetsDir, m_Params.radius);
    printf("[Voxel] stream spawn=(%.1f,%.1f,%.1f) world=%s\n", m_Spawn.x, m_Spawn.y, m_Spawn.z,
           m_Params.worldDir.empty() ? "<no-save>" : m_Params.worldDir.c_str());

    m_PlaceState = m_VoxelRuntime->StreamPlaceState();
    m_PlaceKey = m_VoxelRuntime->GetStateKey(m_PlaceState);
    m_Player.pos = m_Spawn;
    m_CamPos = m_Player.pos + Manro::Vec3(0.f, CPlayer::kEye, 0.f);
    m_Fwd = Manro::Vec3(0, 0, -1);
    m_Yaw = -90.f;
    m_Pitch = -10.f;

    m_Window->CaptureMouse(true);
    m_Window->ShowCursor(false);
    m_bGrabbed = true;

    m_DbgAllowed = m_Params.debug;
    m_Chaos = m_Params.chaos;
}

void CVoxel::OnShutdown() {
    if (m_VoxelRuntime)
        m_VoxelRuntime->Shutdown();
}

bool CVoxel::OnUpdate(const Manro::FrameContext_t &ctx, const Manro::UserCmd_t &) {
    using K = Manro::Key;
    using MB = Manro::MouseButton;

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

    bool justGrabbed = false;
    if (!m_bGrabbed && m_InputManager.IsMouseButtonDown(MB::Left)) {
        m_Window->CaptureMouse(true);
        m_Window->ShowCursor(false);
        m_bGrabbed = true;
        justGrabbed = true;
    }

    if (m_InputManager.IsKeyDown(K::F11) && !m_bF11Held) {
        m_bF11Held = true;
        m_Window->ToggleFullscreen();
    } else if (!m_InputManager.IsKeyDown(K::F11)) {
        m_bF11Held = false;
    }

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

    PlayerUpdate(m_Player, *m_VoxelRuntime, m_InputManager, yawFwd, yawRight, fwd, ctx.DeltaTime,
                 ctx.TotalTime);

    if (m_Chaos) {
        if (m_Frame == 700)
            m_Player.pos += Manro::Vec3(80.f, 0.f, 0.f);
        if (m_Frame == 1400)
            m_Player.pos += Manro::Vec3(0.f, 0.f, -80.f);
        if (m_Frame == 2100)
            m_Player.pos -= Manro::Vec3(80.f, 0.f, 80.f);
    }

    if (m_Player.pos.y < -80.f) {
        m_Player.pos = m_Spawn;
        m_Player.vel = Manro::Vec3(0.f);
    }
    m_CamPos = m_Player.pos + Manro::Vec3(0.f, CPlayer::kEye, 0.f);

    m_BreakCd -= ctx.DeltaTime;
    const Manro::Vec3 eye = m_CamPos;
    if (m_bGrabbed && !justGrabbed && m_InputManager.IsMouseButtonDown(MB::Left) &&
        (!m_bLMBHeld || m_BreakCd <= 0.f)) {
        m_bLMBHeld = true;
        const auto hit = PlayerDetail::RaycastVoxel(*m_VoxelRuntime, eye, fwd, 8.f);
        if (hit.hit) {
            m_VoxelRuntime->QueueEdit(
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
        const auto hit = PlayerDetail::RaycastVoxel(*m_VoxelRuntime, eye, fwd, 8.f);
        if (hit.hit) {
            const Manro::Vec3 target(static_cast<float>(hit.hx + hit.nx),
                                     static_cast<float>(hit.hy + hit.ny),
                                     static_cast<float>(hit.hz + hit.nz));
            if (!m_VoxelRuntime->StreamIsSolid(target)) {
                const Manro::Vec3 hitPos = eye + fwd * hit.t;
                const bool fluid = m_VoxelRuntime->StreamIsFluid(target);
                const Manro::u32 state = VoxelStateMap::Orient(
                    *m_VoxelRuntime, m_PlaceState, m_Fwd, hitPos.y, hit.nx, hit.ny, hit.nz,
                    fluid);
                m_VoxelRuntime->QueueEdit(target + Manro::Vec3(0.5f), 0.f, state, 1);
                ++m_EditCount;
            }
        }
    } else if (!m_InputManager.IsMouseButtonDown(MB::Right)) {
        m_bRMBHeld = false;
    }
    if (m_bGrabbed && !justGrabbed && m_InputManager.IsMouseButtonDown(MB::Middle) &&
        !m_bMMBHeld) {
        m_bMMBHeld = true;
        const auto hit = PlayerDetail::RaycastVoxel(*m_VoxelRuntime, eye, fwd, 8.f);
        if (hit.hit) {
            const Manro::i32 st = m_VoxelRuntime->StreamGetStateAt(
                Manro::Vec3(static_cast<float>(hit.hx) + 0.5f,
                            static_cast<float>(hit.hy) + 0.5f,
                            static_cast<float>(hit.hz) + 0.5f));
            if (st > 0) {
                m_PlaceState = static_cast<Manro::u32>(st);
                m_PlaceKey = m_VoxelRuntime->GetStateKey(m_PlaceState);
                printf("[Voxel] pick state=%u %s\n", m_PlaceState, m_PlaceKey.c_str());
            } else {
                printf("[Voxel] pick: no state (air/unloaded)\n");
            }
        }
    } else if (!m_InputManager.IsMouseButtonDown(MB::Middle)) {
        m_bMMBHeld = false;
    }

    if (m_InputManager.IsKeyDown(K::B) && !m_bBHeld) {
        m_bBHeld = true;
        m_UseBackface = !m_UseBackface;
        m_VoxelRuntime->SetBackfaceEnabled(m_UseBackface);
        printf("[Voxel] backface %s\n", m_UseBackface ? "ON" : "OFF");
    } else if (!m_InputManager.IsKeyDown(K::B)) {
        m_bBHeld = false;
    }
    if (m_InputManager.IsKeyDown(K::N) && !m_bNHeld) {
        m_bNHeld = true;
        m_UseFrustum = !m_UseFrustum;
        m_VoxelRuntime->SetFrustumEnabled(m_UseFrustum);
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
    m_Renderer->SetCameraPosition(m_CamPos);
    return true;
}

void CVoxel::OnRender(Manro::FrameContext_t &frame) {
    const auto t0 = std::chrono::steady_clock::now();
    const float fovTarget = m_Player.sprinting ? 100.f : 90.f;
    m_Fov += (fovTarget - m_Fov) * std::min(1.f, frame.DeltaTime * 8.f);
    if (std::abs(fovTarget - m_Fov) < 0.01f)
        m_Fov = fovTarget;
    const Manro::Mat4 view = glm::lookAt(m_CamPos, m_CamPos + m_Fwd, Manro::Vec3{0, 1, 0});
    const Manro::Mat4 proj =
            glm::perspective(glm::radians(m_Fov), m_Renderer->GetAspectRatio(), 0.1f, 10000.f);
    m_Renderer->SetViewProjection(view, proj);
    m_Renderer->SetCameraPosition(m_CamPos);

    const bool wantLog = (m_Frame % 600) == 0;
    m_VoxelRuntime->SetDebugEnabled(wantLog && m_DbgAllowed);

    m_Renderer->BeginRendering();
    m_Renderer->RenderQueue();
    m_Renderer->EndRendering();
    m_Unfilled = m_VoxelRuntime->GetUnfilledCount();
    m_StreamStats = m_VoxelRuntime->GetStreamStats();
    m_StreamMs = m_StreamStats.totalMs;
    const auto t2 = std::chrono::steady_clock::now();
    const float streamMs = m_StreamMs;
    const float cpuMs =
            std::chrono::duration<float, std::milli>(t2 - t0).count();
    m_CpuEma = (m_CpuEma <= 0.f) ? cpuMs : m_CpuEma + (cpuMs - m_CpuEma) * 0.05f;
    m_StreamEma =
            (m_StreamEma <= 0.f) ? streamMs : m_StreamEma + (streamMs - m_StreamEma) * 0.05f;

    const float dtMs = frame.DeltaTime * 1000.f;
    m_FpsEma = (m_FpsEma <= 0.f) ? dtMs : m_FpsEma + (dtMs - m_FpsEma) * 0.05f;
    m_FrameTimes[m_FrameTimeIdx] = dtMs;
    m_FrameTimeIdx = (m_FrameTimeIdx + 1) % kFrameTimeWindow;
    if (m_FramesSeen < kFrameTimeWindow) ++m_FramesSeen;

    {
        const Manro::FrameStats_t &fst = m_Renderer->GetLastFrameStats();
        float ix = 0.f, id = 0.f, ip = 0.f;
        m_VoxelRuntime->GetGpuInstantTimes(ix, id, ip);
        (void) ix;
        (void) ip;
        if (fst.paceFenceMs > m_PaceFenceMax) m_PaceFenceMax = fst.paceFenceMs;
        if (fst.paceAcquireMs > m_PaceAcquireMax) m_PaceAcquireMax = fst.paceAcquireMs;
        if (fst.pacePresentMs > m_PacePresentMax) m_PacePresentMax = fst.pacePresentMs;
        if (id > m_GpuDrawMax) m_GpuDrawMax = id;
    }

    Manro::u32 dbg[8] = {};
    m_VoxelRuntime->ReadDebugCounters(dbg);

    float worst1 = 0.f, worst01 = 0.f;
    if (m_FramesSeen > 0) {
        float sorted[kFrameTimeWindow];
        for (Manro::u32 i = 0; i < m_FramesSeen; ++i) sorted[i] = m_FrameTimes[i];
        std::sort(sorted, sorted + m_FramesSeen);
        worst1 = sorted[(m_FramesSeen * 99) / 100];
        worst01 = sorted[(m_FramesSeen * 999) / 1000];
    }
    const float emaFps = (m_FpsEma > 0.f) ? 1000.f / m_FpsEma : 0.f;

    DrawHud(emaFps, worst1, worst01, dbg);
    LogStats(emaFps, worst1, worst01, dbg, wantLog);
    ++m_Frame;
}
