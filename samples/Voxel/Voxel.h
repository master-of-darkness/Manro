#pragma once

#include "Player.h"

#include <Manro/Interfaces/IApplication.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/InputBackend.h>
#include <Manro/Platform/Window/Window.h>
#include <Manro/Render/VoxelStreamStats.h>

#include <string>

namespace Manro {
    class CRenderer;
    class CVoxelRuntime;
}

class CVoxel final : public Manro::IApplication {
public:
    struct Params {
        std::string worldDir;
        std::string assetsDir;
        int radius{6};
        float resScale{1.f};
        bool debug{false};
        bool chaos{false};
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

    void OnStartup(const Manro::InitContext_t &ctx) override;

    void OnShutdown() override;

    bool OnUpdate(const Manro::FrameContext_t &ctx, const Manro::UserCmd_t &) override;

    void OnRender(Manro::FrameContext_t &frame) override;

    Manro::CInputManager *GetInputManager() override { return &m_InputManager; }

private:
    void DrawHud(float emaFps, float worst1, float worst01, const Manro::u32 dbg[8]) const;

    void LogStats(float emaFps, float worst1, float worst01, const Manro::u32 dbg[8],
                  bool wantLog);

    Params m_Params;
    Manro::CRenderer *m_Renderer{nullptr};
    Manro::CVoxelRuntime *m_VoxelRuntime{nullptr};
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
    float m_StreamMs{0.f};
    Manro::VoxelStreamStats_t m_StreamStats{};

    static constexpr Manro::u32 kFrameTimeWindow = 240;
    float m_FrameTimes[kFrameTimeWindow]{};
    Manro::u32 m_FrameTimeIdx{0};
    Manro::u32 m_FramesSeen{0};
    float m_FpsEma{0.f};
    float m_CpuEma{0.f};
    float m_StreamEma{0.f};
    float m_PaceFenceMax{0.f};
    float m_PaceAcquireMax{0.f};
    float m_PacePresentMax{0.f};
    float m_GpuDrawMax{0.f};
};
