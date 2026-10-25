#include "Voxel.h"

#include <Manro/Render/VoxelRuntime.h>

#include <imgui.h>

#include <cstdio>

void CVoxel::DrawHud(float emaFps, float worst1, float worst01,
                     const Manro::u32 dbg[8]) const {
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
        ImGui::Text("Bricks: %u (hw %u)  TaskGroups: %u  Edits: %u  Unfilled: %d",
                    m_VoxelRuntime->GetBrickCount(), m_VoxelRuntime->GetBrickHighWater(),
                    m_VoxelRuntime->GetTaskGroups(), m_EditCount, m_Unfilled);
        ImGui::Text("Stream: %.2f ms (r%.2f/e%.2f/n%.2f/d%.2f/c%.2f)",
                    static_cast<double>(m_StreamEma),
                    static_cast<double>(m_StreamStats.rebuildMs),
                    static_cast<double>(m_StreamStats.evictMs),
                    static_cast<double>(m_StreamStats.enqueueMs),
                    static_cast<double>(m_StreamStats.drainMs),
                    static_cast<double>(m_StreamStats.commitMs));
        ImGui::TextDisabled("defer=%u drop=%llu blk=%llu stageHW=%u/%u/%u binds=%u",
                            m_StreamStats.deferredBricks,
                            static_cast<unsigned long long>(m_StreamStats.deferredDropped),
                            static_cast<unsigned long long>(m_StreamStats.blockingUploads),
                            m_StreamStats.stageHighWater[0],
                            m_StreamStats.stageHighWater[1],
                            m_StreamStats.stageHighWater[2], m_StreamStats.midFrameBinds);
        ImGui::TextDisabled("light=%.2fms bricks=%u visits=%llu peakQ=%u dirty=%u",
                            static_cast<double>(m_StreamStats.lightMs),
                            m_StreamStats.lightBricks,
                            static_cast<unsigned long long>(m_StreamStats.lightVisits),
                            m_StreamStats.lightQueuePeak,
                            m_StreamStats.lightDirtyBricks);
        float gx = 0.f, gd = 0.f, gp = 0.f;
        m_VoxelRuntime->GetGpuTimes(gx, gd, gp);
        ImGui::Text("GPU xfer: %.2f ms  draw: %.2f ms  post: %.2f ms", static_cast<double>(gx),
                    static_cast<double>(gd), static_cast<double>(gp));
        ImGui::Text("Player: (%.1f,%.1f,%.1f) %s%s", static_cast<double>(m_Player.pos.x),
                    static_cast<double>(m_Player.pos.y), static_cast<double>(m_Player.pos.z),
                    m_Player.fly
                        ? "FLY"
                        : (m_Player.sprinting
                               ? "SPRINT"
                               : (m_Player.onGround ? "GROUND" : "AIR")),
                    m_bGrabbed ? "" : " (cursor free)");
        ImGui::TextDisabled("task=%u vis=%u faces=%u cull=%u mesh=%u mfaces=%u", dbg[0], dbg[1],
                            dbg[2], dbg[3], dbg[4], dbg[5]);
        ImGui::TextDisabled("ovfBricks=%u dropFaces=%u", dbg[6], dbg[7]);
        ImGui::TextDisabled("backface=%d frustum=%d", m_UseBackface ? 1 : 0,
                            m_UseFrustum ? 1 : 0);
        ImGui::TextDisabled("placing: %s",
                            m_PlaceKey.empty() ? "?" : m_PlaceKey.c_str());
        ImGui::TextDisabled(
            "WASD move | 2xW/Shift/Ctrl sprint | Space jump | F fly | LMB break | RMB place");
        ImGui::TextDisabled("MMB pick block | F11 fullscreen | B/N cull | V vsync | Esc cursor/quit");
    }
    ImGui::End();
}

void CVoxel::LogStats(float emaFps, float worst1, float worst01, const Manro::u32 dbg[8],
                      bool wantLog) {
    if (!wantLog || m_Frame == 0)
        return;

    float lgx = 0.f, lgd = 0.f, lgp = 0.f;
    m_VoxelRuntime->GetGpuTimes(lgx, lgd, lgp);
    Manro::u32 tgOp = 0u, tgCut = 0u, tgSkip = 1u;
    m_VoxelRuntime->GetTaskSplit(tgOp, tgCut, tgSkip);
    printf("[Voxel] fps=%.0f (%.2f ms) 1%%low=%.2fms 0.1%%low=%.2fms bricks=%u (hw %u) "
           "taskGroups=%u (op=%u cut=%u skip=%u) edits=%u unfilled=%d cpu=%.2fms stream=%.2fms gpuxfer=%.2fms "
           "gpudraw=%.2fms gpupost=%.2fms paceF=%.2fms paceA=%.2fms paceP=%.2fms gpuMax=%.2fms "
           "cam=(%.1f,%.1f,%.1f)\n",
           static_cast<double>(emaFps), static_cast<double>(m_FpsEma),
           static_cast<double>(worst1), static_cast<double>(worst01),
           m_VoxelRuntime->GetBrickCount(), m_VoxelRuntime->GetBrickHighWater(),
           m_VoxelRuntime->GetTaskGroups(), tgOp,
           tgCut, tgSkip, m_EditCount,
           m_Unfilled, static_cast<double>(m_CpuEma), static_cast<double>(m_StreamEma),
           static_cast<double>(lgx), static_cast<double>(lgd), static_cast<double>(lgp),
           static_cast<double>(m_PaceFenceMax), static_cast<double>(m_PaceAcquireMax),
           static_cast<double>(m_PacePresentMax), static_cast<double>(m_GpuDrawMax),
           m_CamPos.x, m_CamPos.y, m_CamPos.z);
    printf("[VoxelStream] total=%.2fms rebuild=%.2fms evict=%.2fms enqueue=%.2fms "
           "drain=%.2fms commit=%.2fms defer=%u drop=%llu blk=%llu stageHW=%u/%u/%u binds=%u\n",
           static_cast<double>(m_StreamStats.totalMs),
           static_cast<double>(m_StreamStats.rebuildMs),
           static_cast<double>(m_StreamStats.evictMs),
           static_cast<double>(m_StreamStats.enqueueMs),
           static_cast<double>(m_StreamStats.drainMs),
           static_cast<double>(m_StreamStats.commitMs), m_StreamStats.deferredBricks,
           static_cast<unsigned long long>(m_StreamStats.deferredDropped),
           static_cast<unsigned long long>(m_StreamStats.blockingUploads),
           m_StreamStats.stageHighWater[0], m_StreamStats.stageHighWater[1],
           m_StreamStats.stageHighWater[2], m_StreamStats.midFrameBinds);
    printf("[VoxelStream][light] ms=%.2f bricks=%u visits=%llu peakQ=%u dirty=%u\n",
           static_cast<double>(m_StreamStats.lightMs), m_StreamStats.lightBricks,
           static_cast<unsigned long long>(m_StreamStats.lightVisits),
           m_StreamStats.lightQueuePeak, m_StreamStats.lightDirtyBricks);
    m_PaceFenceMax = 0.f;
    m_PaceAcquireMax = 0.f;
    m_PacePresentMax = 0.f;
    m_GpuDrawMax = 0.f;
    if (m_DbgAllowed)
        printf("[VoxelDbg] taskRuns=%u visible=%u faces=%u culled=%u meshRuns=%u "
               "meshFaces=%u\n",
               dbg[0], dbg[1], dbg[2], dbg[3], dbg[4], dbg[5]);
}
