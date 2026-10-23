#include <Manro/Render/VoxelRuntime.h>

#include "VoxelBlocks/VoxelBlockAssets.h"
#include "VoxelContent/VoxelStreamWorld.h"
#include "VoxelDraw/VoxelRenderer.h"
#include "VoxelStorage/VoxelBrickStore.h"
#include "VoxelStorage/VoxelEdit.h"

#include <Manro/Core/Logger.h>

#include <cmath>
#include <utility>

namespace Manro {
    struct CVoxelRuntime::Impl {
        CVulkanContext &ctx;
        CVirtualFS &vfs;
        CPipelineCache &cache;
        std::unique_ptr<CVoxelRenderer> renderer;
        std::unique_ptr<CVoxelStreamWorld> streamWorld;
        int lastUnfilled{0};
        bool initialized{false};

        Impl(CVulkanContext &c, CVirtualFS &v, CPipelineCache &pc)
            : ctx(c), vfs(v), cache(pc) {}
    };

    CVoxelRuntime::CVoxelRuntime(CVulkanContext &ctx, CVirtualFS &vfs,
                                 CPipelineCache &cache)
        : m_Impl(std::make_unique<Impl>(ctx, vfs, cache)) {}

    CVoxelRuntime::~CVoxelRuntime() { Shutdown(); }

    void CVoxelRuntime::Init(u32 width, u32 height) {
        if (!m_Impl || m_Impl->initialized)
            return;
        m_Impl->renderer = std::make_unique<CVoxelRenderer>(m_Impl->ctx, m_Impl->vfs);
        m_Impl->renderer->Init(m_Impl->cache, width, height);
        m_Impl->initialized = true;
    }

    void CVoxelRuntime::Shutdown() {
        if (!m_Impl)
            return;
        if (m_Impl->renderer)
            m_Impl->renderer->WaitIdle();
        m_Impl->streamWorld.reset();
        m_Impl->renderer.reset();
        m_Impl->initialized = false;
        m_Impl->lastUnfilled = 0;
    }

    bool CVoxelRuntime::IsInitialized() const {
        return m_Impl && m_Impl->initialized && m_Impl->renderer;
    }

    void CVoxelRuntime::AllocateBrick(u32 bx, u32 by, u32 bz) {
        if (IsInitialized())
            m_Impl->renderer->GetWorld().AllocateBrick(bx, by, bz);
    }

    void CVoxelRuntime::UploadBrick(u32 brickIdx, const u16 *mats, const u32 *occupancy) {
        if (IsInitialized())
            m_Impl->renderer->GetWorld().UploadBrickData(brickIdx, mats, occupancy);
    }

    void CVoxelRuntime::QueueEdit(const Vec3 &pos, float radius, u32 material, u32 op) {
        if (!IsInitialized())
            return;
        VoxelEditCmd_t cmd{};
        cmd.pos = pos;
        cmd.radius = radius;
        cmd.material = material;
        cmd.op = op;
        CVoxelBrickStore &world = m_Impl->renderer->GetWorld();
        world.QueueEdit(cmd);
        if (m_Impl->streamWorld)
            m_Impl->streamWorld->ApplyEdit(world, pos, radius, op, material);
    }

    u32 CVoxelRuntime::GetBrickCount() const {
        return IsInitialized() ? m_Impl->renderer->GetWorld().GetBrickCount() : 0u;
    }

    u32 CVoxelRuntime::GetTaskGroups() const {
        return IsInitialized() ? m_Impl->renderer->GetStats().taskGroups : 0u;
    }

    void CVoxelRuntime::GetTaskSplit(u32 &opaque, u32 &cutout, u32 &skipped) const {
        if (!IsInitialized()) {
            opaque = 0u;
            cutout = 0u;
            skipped = 1u;
            return;
        }
        opaque = m_Impl->renderer->GetStats().taskGroupsOpaque;
        cutout = m_Impl->renderer->GetStats().taskGroupsCutout;
        skipped = m_Impl->renderer->GetStats().cutoutSkipped;
    }

    void CVoxelRuntime::GetGpuTimes(float &xferMs, float &drawMs, float &postMs) const {
        if (!IsInitialized()) {
            xferMs = 0.f;
            drawMs = 0.f;
            postMs = 0.f;
            return;
        }
        xferMs = m_Impl->renderer->GetStats().gpuXferMs;
        drawMs = m_Impl->renderer->GetStats().gpuDrawMs;
        postMs = m_Impl->renderer->GetStats().gpuPostMs;
    }

    void CVoxelRuntime::GetGpuInstantTimes(float &xferMs, float &drawMs,
                                           float &postMs) const {
        if (!IsInitialized()) {
            xferMs = 0.f;
            drawMs = 0.f;
            postMs = 0.f;
            return;
        }
        xferMs = m_Impl->renderer->GetStats().gpuXferInstMs;
        drawMs = m_Impl->renderer->GetStats().gpuDrawInstMs;
        postMs = m_Impl->renderer->GetStats().gpuPostInstMs;
    }

    void CVoxelRuntime::ReadDebugCounters(u32 out[8]) const {
        if (!IsInitialized()) {
            for (int i = 0; i < 8; ++i)
                out[i] = 0u;
            return;
        }
        m_Impl->renderer->ReadDebugCounters(out);
    }

    void CVoxelRuntime::SetDebugEnabled(bool enabled) {
        if (IsInitialized())
            m_Impl->renderer->SetDebugEnabled(enabled);
    }

    void CVoxelRuntime::SetBackfaceEnabled(bool enabled) {
        if (IsInitialized())
            m_Impl->renderer->SetBackfaceEnabled(enabled);
    }

    void CVoxelRuntime::SetFrustumEnabled(bool enabled) {
        if (IsInitialized())
            m_Impl->renderer->SetFrustumEnabled(enabled);
    }

    Vec3 CVoxelRuntime::StreamInit(std::unique_ptr<IVoxelContentSource> content,
                                   const std::string &worldDir,
                                   const std::string &assetsDir, int radiusSections) {
        if (!IsInitialized())
            return Vec3(8.f, 80.f, 8.f);

        m_Impl->renderer->WaitIdle();
        m_Impl->streamWorld.reset();

        std::vector<VoxelBlockState> saveStates;
        if (content && !content->ScanSaveStates(saveStates)) {
            LOG_ERROR("[CVoxelRuntime] Save scan failed");
            return Vec3(8.f, 80.f, 8.f);
        }

        const std::string dir = assetsDir.empty() ? MANRO_MC_ASSETS_DIR : assetsDir;
        std::string err;
        BlockAssetPack_t pack;
        if (!BuildBlockAssetPack(dir, saveStates, pack, err)) {
            LOG_ERROR("[CVoxelRuntime] Block assets failed: {}", err);
            return Vec3(8.f, 80.f, 8.f);
        }
        if (!m_Impl->renderer->LoadBlockAssets(pack)) {
            LOG_ERROR("[CVoxelRuntime] State id {} exceeds 15-bit face-cache pack",
                      pack.maxState);
            return Vec3(8.f, 80.f, 8.f);
        }

        m_Impl->streamWorld = std::make_unique<CVoxelStreamWorld>();
        VoxelStreamDesc_t desc{};
        desc.worldDir = worldDir;
        desc.radiusSections = radiusSections;
        return m_Impl->streamWorld->Init(m_Impl->renderer->GetWorld(),
                                         m_Impl->renderer->GetBlockPack(), desc,
                                         std::move(content));
    }

    int CVoxelRuntime::StreamUpdate(const Vec3 &cameraPos, u32 flightSlot) {
        if (!IsInitialized() || !m_Impl->streamWorld) {
            if (m_Impl)
                m_Impl->lastUnfilled = 0;
            return 0;
        }
        m_Impl->lastUnfilled = m_Impl->streamWorld->Update(
            m_Impl->renderer->GetWorld(), cameraPos, flightSlot);
        return m_Impl->lastUnfilled;
    }

    int CVoxelRuntime::GetUnfilledCount() const {
        return m_Impl ? m_Impl->lastUnfilled : 0;
    }

    const VoxelStreamStats_t &CVoxelRuntime::GetStreamStats() const {
        static const VoxelStreamStats_t kEmpty{};
        return m_Impl->streamWorld ? m_Impl->streamWorld->GetLastStats() : kEmpty;
    }

    bool CVoxelRuntime::StreamIsSolid(const Vec3 &p) const {
        if (!m_Impl->streamWorld)
            return true;
        return m_Impl->streamWorld->IsSolidAt(static_cast<i64>(std::floor(p.x)),
                                              static_cast<i64>(std::floor(p.y)),
                                              static_cast<i64>(std::floor(p.z)));
    }

    bool CVoxelRuntime::StreamIsFluid(const Vec3 &p) const {
        if (!m_Impl->streamWorld)
            return false;
        return m_Impl->streamWorld->IsFluidAt(static_cast<i64>(std::floor(p.x)),
                                              static_cast<i64>(std::floor(p.y)),
                                              static_cast<i64>(std::floor(p.z)));
    }

    u32 CVoxelRuntime::StreamPlaceState() const {
        if (!m_Impl->streamWorld)
            return 1u;
        const i32 st = m_Impl->streamWorld->PlaceState();
        return st >= 0 ? static_cast<u32>(st) : 1u;
    }

    i32 CVoxelRuntime::StreamGetStateAt(const Vec3 &p) const {
        if (!m_Impl->streamWorld)
            return -1;
        return m_Impl->streamWorld->GetStateAt(static_cast<i64>(std::floor(p.x)),
                                               static_cast<i64>(std::floor(p.y)),
                                               static_cast<i64>(std::floor(p.z)));
    }

    bool CVoxelRuntime::StreamGetCollisionBox(const Vec3 &p, Vec3 &mn, Vec3 &mx) const {
        if (!m_Impl->streamWorld)
            return false;
        return m_Impl->streamWorld->GetCollisionBoxAt(static_cast<i64>(std::floor(p.x)),
                                                      static_cast<i64>(std::floor(p.y)),
                                                      static_cast<i64>(std::floor(p.z)), mn, mx);
    }

    std::string CVoxelRuntime::GetStateKey(u32 id) const {
        return IsInitialized() ? m_Impl->renderer->GetStateKey(id) : std::string{};
    }

    u32 CVoxelRuntime::FindStateByKey(const std::string &key) const {
        return IsInitialized() ? m_Impl->renderer->FindStateByKey(key) : ~0u;
    }

    void CVoxelRuntime::Record(CVoxelEncoder &enc, const VoxelFrameDesc &frame) {
        if (IsInitialized())
            m_Impl->renderer->Record(enc, frame, m_Impl->streamWorld.get());
    }

    void CVoxelRuntime::StampPost(CVoxelEncoder &enc, u32 flightSlot, u32 subIdx) {
        if (IsInitialized())
            m_Impl->renderer->StampPost(enc, flightSlot, subIdx);
    }
}
