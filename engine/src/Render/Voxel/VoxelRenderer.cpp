#include "VoxelRenderer.h"
#include "VoxelWorld.h"
#include "VoxelStreamWorld.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Pipeline.h"
#include "../Vulkan/PipelineCache.h"
#include "../Vulkan/Buffer.h"
#include "../Vulkan/GpuDefs.h"
#include "../Vulkan/TileArray.h"
#include "../Vulkan/GpuTimer.h"
#include "../Vulkan/VoxelEncoder.h"
#include "../Vulkan/VoxelPipelines.h"

#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Manro {
    CVoxelRenderer::CVoxelRenderer(CVulkanContext &ctx, CVirtualFS &vfs)
        : m_Context(ctx), m_Vfs(vfs) {
        m_World = CreateScope<CVoxelWorld>(ctx);
    }

    CVoxelRenderer::~CVoxelRenderer() { Shutdown(); }

    void CVoxelRenderer::Init(CPipelineCache &cache, u32 width, u32 height) {
        m_Context.RequireMeshShaders("Voxel");
        VoxelWorldDesc_t desc{};
        desc.virtualDim = 64;
        desc.voxelSize = 1.f;
        desc.maxResidentBricks = 8192;
        m_World->Init(desc);

        m_Tiles = CreateScope<CTileArray>(m_Context);
        m_Tiles->Ensure(1);
        BuildPipelines(cache);

        for (u32 i = 0; i < CVoxelRenderer::kFlightSlots; ++i) {
            m_VisibilityRing[i] = CreateScope<CBuffer>(
                m_Context, sizeof(u32) * desc.maxResidentBricks,
                EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);
        }

        m_PaletteBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(Vec4) * 512,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);
        m_SunBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(Vec4) * 2,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);
        m_FragParams = CreateScope<CBuffer>(
            m_Context, sizeof(VoxelFragParams_t),
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);
        const u32 cascadeRes = 64;
        m_CascadeBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(Vec4) * cascadeRes * cascadeRes * cascadeRes * 2,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::GpuOnly);
        m_EditStaging = CreateScope<CBuffer>(
            m_Context, sizeof(VoxelEditCmd_t) * 1024,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);
        m_FrameParamsRing[0] = CreateScope<CBuffer>(
            m_Context, sizeof(VoxelFrameParams_t),
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);
        for (u32 i = 1; i < CVoxelRenderer::kFlightSlots; ++i) {
            m_FrameParamsRing[i] = CreateScope<CBuffer>(
                m_Context, sizeof(VoxelFrameParams_t),
                EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);
        }
        for (u32 i = 0; i < CVoxelRenderer::kFlightSlots; ++i) {
            m_CutoutParamsRing[i] = CreateScope<CBuffer>(
                m_Context, sizeof(VoxelFrameParams_t),
                EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);
        }

        m_FaceCache = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * kVoxelFaceCacheStride * desc.maxResidentBricks,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::GpuOnly);

        m_SpillCache = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * kVoxelSpillQuota * kVoxelSpillWords * desc.maxResidentBricks,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::GpuOnly);
        m_SpillDesc = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * kVoxelSpillDescWords * desc.maxResidentBricks,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::GpuOnly);

        m_DebugReadback = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 8,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress | EBufferUsage::TransferDst,
            EBufferMemory::CpuToGpu);

        m_BlockTileTable = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 32768 * 6,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);
        m_BlockFlagsTable = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 32768,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);

        m_BlockShapeTable = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 32768 * 2,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);

        m_BlockUvTable = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 32768 * 6,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);

        m_BlockBoxTable = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * kBlockStateMax * kBlockBoxWordsPerState,
            EBufferUsage::Storage | EBufferUsage::ShaderAddress, EBufferMemory::CpuToGpu);

        m_Timer = CreateScope<CGpuTimer>(m_Context);
        m_Timer->Init(kFlightSlots, kQueriesPerSlot);

        {
            BlockAssetPack_t fallback{};
            fallback.tiles.emplace_back(16 * 16 * 4, 0);
            for (int i = 0; i < 16 * 16; ++i) {
                fallback.tiles[0][i * 4 + 0] = 255;
                fallback.tiles[0][i * 4 + 2] = 255;
                fallback.tiles[0][i * 4 + 3] = 255;
            }
            fallback.tilePaths.emplace_back();
            fallback.states.resize(32768);
            CreateBlockTiles(fallback);
            UploadBlockTables();
        }

        Vec4 sun[2] = {Vec4(0.3f, -1.f, 0.2f, 0.f), Vec4(1.f, 0.98f, 0.9f, 1.f)};
        {
            const float len =
                std::sqrt(sun[0].x * sun[0].x + sun[0].y * sun[0].y + sun[0].z * sun[0].z);
            if (len > 1e-6f) {
                sun[0].x /= len;
                sun[0].y /= len;
                sun[0].z /= len;
            }
        }
        m_SunBuffer->LoadData(sun, sizeof(sun));
        m_SunDir = sun[0];
        m_SunColor = sun[1];
        {
            VoxelFragParams_t fp{};
            fp.sunDir = m_SunDir;
            fp.sunColor = m_SunColor;
            m_FragParams->LoadData(&fp, sizeof(fp));
        }

        {
            std::vector<Vec4> palette(512, Vec4(0.6f, 0.6f, 0.6f, 1.f));
            palette[0] = Vec4(0.85f, 0.25f, 0.15f, 1.f);
            palette[1] = Vec4(0.1f, 0.7f, 0.65f, 1.f);
            m_PaletteBuffer->LoadData(palette.data(), sizeof(Vec4) * palette.size());
        }

        m_VisibleList.reserve(desc.maxResidentBricks);
        m_SortScratch.reserve(desc.maxResidentBricks);

        m_bInitialized = true;
        LOG_INFO("[CVoxelRenderer] Initialized (sparse bricks, task/mesh PSO, {}x{})", width, height);
    }

    void CVoxelRenderer::Shutdown() {
        m_Context.WaitIdle();
        m_Tiles.reset();
        m_Timer.reset();
        m_StreamWorld.reset();
        m_BlockTileTable.reset();
        m_BlockFlagsTable.reset();
        m_BlockShapeTable.reset();
        m_BlockUvTable.reset();
        m_BlockBoxTable.reset();
        m_OpaquePipeline.reset();
        m_CutoutPipeline.reset();
        m_EditPipeline.reset();
        m_GiInjectPipeline.reset();
        m_GiPropagatePipeline.reset();
        for (auto &slot : m_VisibilityRing)
            slot.reset();
        m_PaletteBuffer.reset();
        m_SunBuffer.reset();
        m_FragParams.reset();
        m_CascadeBuffer.reset();
        m_EditStaging.reset();
        for (auto &slot : m_FrameParamsRing)
            slot.reset();
        for (auto &slot: m_CutoutParamsRing)
            slot.reset();
        m_DebugReadback.reset();
        m_FaceCache.reset();
        m_SpillCache.reset();
        m_SpillDesc.reset();
        if (m_World)
            m_World->Shutdown();
        m_bInitialized = false;
        m_TileLayers = 0;
    }

    void CVoxelRenderer::BuildPipelines(CPipelineCache &cache) {
        auto taskSpv = m_Vfs.ReadFile("shaders://voxel_task.task.spv");
        auto meshSpv = m_Vfs.ReadFile("shaders://voxel_mesh.mesh.spv");
        auto fragOpaqueSpv = m_Vfs.ReadFile("shaders://voxel_shade_opaque.frag.spv");
        auto fragCutoutSpv = m_Vfs.ReadFile("shaders://voxel_shade_cutout.frag.spv");
        auto editSpv = m_Vfs.ReadFile("shaders://voxel_edit.comp.spv");
        auto giInjectSpv = m_Vfs.ReadFile("shaders://voxel_gi_inject.comp.spv");
        auto giPropSpv = m_Vfs.ReadFile("shaders://voxel_gi_propagate.comp.spv");

        if (taskSpv.empty() || meshSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel task/mesh shaders missing");
        if (fragOpaqueSpv.empty() || fragCutoutSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel shade fragments missing");
        if (editSpv.empty() || giInjectSpv.empty() || giPropSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel compute shaders missing");

        BuildVoxelPipelines(m_Context, cache, *m_Tiles, taskSpv, meshSpv, fragOpaqueSpv,
                            fragCutoutSpv, editSpv, giInjectSpv, giPropSpv, m_OpaquePipeline,
                            m_CutoutPipeline, m_EditPipeline, m_GiInjectPipeline,
                            m_GiPropagatePipeline);
    }

    void CVoxelRenderer::CreateBlockTiles(const BlockAssetPack_t &pack) {
        const u32 layers = std::max<u32>(1u, static_cast<u32>(pack.tiles.size()));
        constexpr u32 kMips = 5;
        m_TileLayers = layers;

        constexpr u32 kLevelPx[5] = {256, 64, 16, 4, 1};
        u64 layerBytes = 0;
        for (u32 m = 0; m < kMips; ++m)
            layerBytes += static_cast<u64>(kLevelPx[m]) * 4u;
        std::vector<u8> staging(static_cast<size_t>(layerBytes) * layers);
        std::vector<u32> prev(256), cur(256);
        for (u32 l = 0; l < layers; ++l) {
            const u8 *src = (l < pack.tiles.size() && pack.tiles[l].size() >= 16 * 16 * 4)
                                ? pack.tiles[l].data()
                                : pack.tiles[0].data();
            for (int i = 0; i < 256; ++i)
                prev[static_cast<size_t>(i)] = static_cast<u32>(src[i * 4 + 0]) |
                                               (static_cast<u32>(src[i * 4 + 1]) << 8u) |
                                               (static_cast<u32>(src[i * 4 + 2]) << 16u) |
                                               (static_cast<u32>(src[i * 4 + 3]) << 24u);
            size_t dstOff = static_cast<size_t>(layerBytes) * l;
            u32 dim = 16;
            for (u32 m = 0; m < kMips; ++m) {
                for (u32 i = 0; i < dim * dim; ++i) {
                    const u32 p = prev[i];
                    staging[dstOff + i * 4 + 0] = static_cast<u8>(p & 0xFFu);
                    staging[dstOff + i * 4 + 1] = static_cast<u8>((p >> 8u) & 0xFFu);
                    staging[dstOff + i * 4 + 2] = static_cast<u8>((p >> 16u) & 0xFFu);
                    staging[dstOff + i * 4 + 3] = static_cast<u8>((p >> 24u) & 0xFFu);
                }
                dstOff += static_cast<size_t>(dim) * dim * 4u;
                if (m + 1u < kMips) {
                    const u32 nd = dim / 2u;
                    for (u32 y = 0; y < nd; ++y) {
                        for (u32 x = 0; x < nd; ++x) {
                            u32 acc[4] = {0, 0, 0, 0};
                            for (u32 dy = 0; dy < 2; ++dy) {
                                for (u32 dx = 0; dx < 2; ++dx) {
                                    const u32 p = prev[(y * 2u + dy) * dim + (x * 2u + dx)];
                                    acc[0] += p & 0xFFu;
                                    acc[1] += (p >> 8u) & 0xFFu;
                                    acc[2] += (p >> 16u) & 0xFFu;
                                    acc[3] += (p >> 24u) & 0xFFu;
                                }
                            }
                            cur[y * nd + x] = ((acc[0] + 2u) / 4u) | (((acc[1] + 2u) / 4u) << 8u) |
                                              (((acc[2] + 2u) / 4u) << 16u) |
                                              (((acc[3] + 2u) / 4u) << 24u);
                        }
                    }
                    prev = cur;
                    dim = nd;
                }
            }
        }
        m_Tiles->Upload(layers, staging.data(), static_cast<u64>(staging.size()), layerBytes);
        m_TileLayers = m_Tiles->Layers();
    }

    void CVoxelRenderer::UploadBlockTables() {
        std::vector<u32> tiles(32768u * 6u, 0u), flags(32768u, 0u), shapes(32768u * 2u, 0u),
            uvs(32768u * 6u, 0xFFFF0000u),
            boxes(kBlockStateMax * kBlockBoxWordsPerState, 0u);

        for (size_t s = 0; s < 32768u; ++s) {
            shapes[s * 2u + 0u] = 0u;
            shapes[s * 2u + 1u] = 16u | (16u << 8u) | (16u << 16u);
        }
        const size_t n = std::min(m_BlockPack.states.size(), static_cast<size_t>(32768u));
        for (size_t s = 0; s < n; ++s) {
            for (int f = 0; f < 6; ++f)
                tiles[s * 6u + static_cast<size_t>(f)] = m_BlockPack.states[s].faces.tile[f];
            flags[s] = m_BlockPack.states[s].flags;
            const auto &look = m_BlockPack.states[s];
            shapes[s * 2u + 0u] = static_cast<u32>(look.shapeMin[0]) |
                                   (static_cast<u32>(look.shapeMin[1]) << 8u) |
                                   (static_cast<u32>(look.shapeMin[2]) << 16u);
            shapes[s * 2u + 1u] = static_cast<u32>(look.shapeMax[0]) |
                                   (static_cast<u32>(look.shapeMax[1]) << 8u) |
                                   (static_cast<u32>(look.shapeMax[2]) << 16u);
            for (int f = 0; f < 6; ++f)
                uvs[s * 6u + static_cast<size_t>(f)] = look.uvPacked[f];
        }
        const size_t nb =
            std::min(m_BlockPack.boxTable.size(), boxes.size());
        for (size_t i = 0; i < nb; ++i)
            boxes[i] = m_BlockPack.boxTable[i];
        bool hasCutout = false;
        for (size_t s = 0; s < n; ++s) {
            if ((flags[s] & kBlockFlagCutout) != 0u) {
                hasCutout = true;
                break;
            }
        }
        m_HasAnyCutout = hasCutout;
        m_BlockTileTable->LoadData(tiles.data(), tiles.size() * sizeof(u32));
        m_BlockFlagsTable->LoadData(flags.data(), flags.size() * sizeof(u32));
        m_BlockShapeTable->LoadData(shapes.data(), shapes.size() * sizeof(u32));
        m_BlockUvTable->LoadData(uvs.data(), uvs.size() * sizeof(u32));
        m_BlockBoxTable->LoadData(boxes.data(), boxes.size() * sizeof(u32));
    }

    Vec3 CVoxelRenderer::StreamInit(const std::string &worldDir, const std::string &assetsDir,
                                int radiusSections) {

        m_Context.WaitIdle();

        m_StreamWorld.reset();

        const std::string dir = assetsDir.empty() ? MANRO_MC_ASSETS_DIR : assetsDir;
        std::string err;
        if (!BuildBlockAssetPack(dir, worldDir, m_BlockPack, err)) {
            LOG_ERROR("[CVoxelRenderer] Block assets failed: {}", err);
            return Vec3(8.f, 80.f, 8.f);
        }
        if (m_BlockPack.maxState >= 32768u) {
            LOG_ERROR("[CVoxelRenderer] State id {} exceeds 15-bit face-cache pack", m_BlockPack.maxState);
            return Vec3(8.f, 80.f, 8.f);
        }
        CreateBlockTiles(m_BlockPack);
        UploadBlockTables();
        m_StreamWorld = std::make_unique<CVoxelStreamWorld>();
        VoxelStreamDesc_t desc{};
        desc.worldDir = worldDir;
        desc.radiusSections = radiusSections;
        return m_StreamWorld->Init(*m_World, m_BlockPack, desc);
    }

    int CVoxelRenderer::StreamUpdate(const Vec3 &cameraPos, u32 flightSlot) {
        if (!m_StreamWorld)
            return 0;
        return m_StreamWorld->Update(*m_World, cameraPos, flightSlot);
    }

    const VoxelStreamStats_t &CVoxelRenderer::GetStreamStats() const {
        static const VoxelStreamStats_t kEmpty{};
        return m_StreamWorld ? m_StreamWorld->GetLastStats() : kEmpty;
    }

    bool CVoxelRenderer::StreamIsSolid(const Vec3 &p) const {
        if (!m_StreamWorld)
            return true;
        return m_StreamWorld->IsSolidAt(static_cast<i64>(std::floor(p.x)),
                                    static_cast<i64>(std::floor(p.y)),
                                    static_cast<i64>(std::floor(p.z)));
    }

    bool CVoxelRenderer::StreamIsFluid(const Vec3 &p) const {
        if (!m_StreamWorld)
            return false;
        return m_StreamWorld->IsFluidAt(static_cast<i64>(std::floor(p.x)),
                                    static_cast<i64>(std::floor(p.y)),
                                    static_cast<i64>(std::floor(p.z)));
    }

    u32 CVoxelRenderer::StreamPlaceState() const {
        if (!m_StreamWorld)
            return 1u;
        const i32 st = m_StreamWorld->PlaceState();
        return st >= 0 ? static_cast<u32>(st) : 1u;
    }

    void CVoxelRenderer::StreamApplyEdit(const Vec3 &pos, float radius, u32 op, u32 state) {
        if (m_StreamWorld)
            m_StreamWorld->ApplyEdit(*m_World, pos, radius, op, state);
    }

    i32 CVoxelRenderer::StreamGetStateAt(const Vec3 &p) const {
        if (!m_StreamWorld)
            return -1;
        return m_StreamWorld->GetStateAt(static_cast<i64>(std::floor(p.x)),
                                         static_cast<i64>(std::floor(p.y)),
                                         static_cast<i64>(std::floor(p.z)));
    }

    bool CVoxelRenderer::StreamGetCollisionBox(const Vec3 &p, Vec3 &mn, Vec3 &mx) const {
        if (!m_StreamWorld)
            return false;
        return m_StreamWorld->GetCollisionBoxAt(static_cast<i64>(std::floor(p.x)),
                                                static_cast<i64>(std::floor(p.y)),
                                                static_cast<i64>(std::floor(p.z)), mn, mx);
    }

    std::string CVoxelRenderer::GetStateKey(u32 id) const {
        if (id < m_BlockPack.stateKeys.size())
            return m_BlockPack.stateKeys[id];
        return {};
    }

    u32 CVoxelRenderer::FindStateByKey(const std::string &key) const {
        const auto it = m_BlockPack.stateByKey.find(key);
        if (it != m_BlockPack.stateByKey.end())
            return it->second;
        return ~0u;
    }

    void CVoxelRenderer::DispatchEdits(CVoxelEncoder &enc) {
        const auto &edits = m_World->GetPendingEdits();
        if (edits.empty() || !m_EditPipeline->IsValid())
            return;
        m_EditStaging->LoadData(edits.data(), sizeof(VoxelEditCmd_t) * edits.size());

        VoxelEditDispatch desc{};
        desc.brickBufferAddr = m_World->GetBrickBufferAddr();
        desc.headerAddr = m_World->GetHeaderAddr();
        desc.pageTableAddr = m_World->GetPageTableAddr();
        desc.editAddr = m_EditStaging->Address();
        desc.worldMin = m_World->GetWorldMin();
        desc.brickSize = m_World->GetBrickSize();
        desc.editCount = static_cast<u32>(edits.size());
        desc.virtualDim = m_World->GetVirtualDim();

        enc.DispatchEdit(*m_EditPipeline, *m_EditStaging, desc);
    }

    void CVoxelRenderer::DispatchGi(CVoxelEncoder &enc) {

        if (!m_bGiEnabled)
            return;
        if (!m_GiInjectPipeline->IsValid() || !m_GiPropagatePipeline->IsValid())
            return;
        const u32 cascadeRes = 64;
        const u64 cascadeBytes =
            sizeof(Vec4) * cascadeRes * cascadeRes * cascadeRes;
        VoxelGiDispatch desc{};
        desc.brickBufferAddr = m_World->GetBrickBufferAddr();
        desc.headerAddr = m_World->GetHeaderAddr();
        desc.cascadeAddr = m_CascadeBuffer->Address();
        desc.cascadeDstAddr = m_CascadeBuffer->Address() + cascadeBytes;
        desc.sunAddr = m_SunBuffer->Address();
        desc.brickCount = m_World->GetBrickCount();
        desc.cascadeRes = cascadeRes;
        desc.cascadeCount = 1;
        desc.brickSize = m_World->GetBrickSize();
        desc.worldMin = m_World->GetWorldMin();

        const u32 groups = (cascadeRes + 7u) / 8u;
        enc.DispatchGi(*m_GiInjectPipeline, desc, groups);
        enc.BarrierGiToGi();
        enc.DispatchGi(*m_GiPropagatePipeline, desc, groups);
        enc.BarrierGiToFrag();
    }

    u64 CVoxelRenderer::GetGiSampleAddr() const {
        if (!m_bGiEnabled || !m_CascadeBuffer)
            return 0;
        const u32 cascadeRes = 64;
        const u64 cascadeBytes =
            sizeof(Vec4) * cascadeRes * cascadeRes * cascadeRes;
        return m_CascadeBuffer->Address() + cascadeBytes;
    }

    void CVoxelRenderer::Record(CVoxelEncoder &enc, const VoxelFrameDesc &frame) {
        if (!m_bInitialized)
            return;

        const u32 flightSlot = frame.flightSlot;
        const Mat4 &viewProj = frame.viewProj;
        const Mat4 &prevViewProj = frame.prevViewProj;
        const Vec3 &cameraPos = frame.cameraPos;
        const float maxDrawDist = frame.farZ > 0.f ? frame.farZ : 10000.f;

        ++m_RecordTick;
        const u32 qslot = flightSlot % kFlightSlots;
        if (m_Timer && m_Timer->Valid() && m_Timer->PeriodNs() > 0.f && (m_RecordTick & 1u) == 0u) {
            u64 ticks[kQueriesPerSlot]{0, 0, 0, 0, 0, 0};
            if (m_Timer->Poll(qslot, ticks, kQueriesPerSlot)) {
                const float msPerTick = m_Timer->PeriodNs() / 1000000.f;
                const float xfer = static_cast<float>(ticks[1] - ticks[0]) * msPerTick;
                const float draw = static_cast<float>(ticks[2] - ticks[1]) * msPerTick;
                const float post = static_cast<float>(ticks[5] - ticks[3]) * msPerTick;
                if (xfer >= 0.f && xfer < 100.f && draw >= 0.f && draw < 100.f &&
                    post >= 0.f && post < 100.f) {
                    constexpr float kTsAlpha = 0.1f;
                    m_Stats.gpuXferMs += (xfer - m_Stats.gpuXferMs) * kTsAlpha;
                    m_Stats.gpuDrawMs += (draw - m_Stats.gpuDrawMs) * kTsAlpha;
                    m_Stats.gpuPostMs += (post - m_Stats.gpuPostMs) * kTsAlpha;
                    m_Stats.gpuXferInstMs = xfer;
                    m_Stats.gpuDrawInstMs = draw;
                    m_Stats.gpuPostInstMs = post;
                }
            }
        }

        m_World->FlushHeaders();
        DispatchEdits(enc);
        DispatchGi(enc);
        m_World->ClearEdits();

        m_World->FlushStagedUploads(enc, flightSlot);

        const u32 brickCount = m_World->GetBrickCount();
        m_Stats.brickCount = brickCount;
        m_Stats.editCount = 0;
        if (brickCount == 0 || !m_OpaquePipeline->IsValid() || !m_CutoutPipeline->IsValid())
            return;

        if (m_Timer && m_Timer->Valid()) {
            m_Timer->Reset(enc, qslot);
            m_Timer->Stamp(enc, qslot, 0, true);
        }

        u32 visibleCount = brickCount;

        CBuffer &visSlot = *m_VisibilityRing[flightSlot % kFlightSlots];
        {
            const float brickSize = m_World->GetBrickSize();
            const float radius = brickSize * 0.8660254f;
            const u64 headerVersion = m_World->GetHeaderVersion();
            const bool cacheHit =
                m_VisCacheValid && m_CachedUseFrustum == m_bUseFrustum &&
                m_CachedBrickCount == brickCount && m_CachedHeaderVersion == headerVersion &&
                m_CachedCameraPos == cameraPos && m_CachedViewProj == viewProj;
            if (!cacheHit) {
                const float maxDistWithRadius = maxDrawDist + radius;
                const float maxDistSq = maxDistWithRadius * maxDistWithRadius;
                m_VisibleList.clear();
                if (m_bUseFrustum) {
                    Vec4 rows[4];
                    for (int r = 0; r < 4; ++r)
                        rows[r] = Vec4(viewProj[0][r], viewProj[1][r], viewProj[2][r], viewProj[3][r]);
                    Vec4 planes[6] = {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1],
                                      rows[3] - rows[1], rows[3] + rows[2], rows[3] - rows[2]};
                    for (auto &pl : planes) {
                        const float len = std::sqrt(pl.x * pl.x + pl.y * pl.y + pl.z * pl.z);
                        if (len > 1e-6f) {
                            const float inv = 1.f / len;
                            pl.x *= inv;
                            pl.y *= inv;
                            pl.z *= inv;
                            pl.w *= inv;
                        }
                    }
                    const auto &mirror = m_World->GetHeaderMirror();
                    for (u32 i = 0; i < brickCount; ++i) {
                        if (m_World->IsBrickHidden(i))
                            continue;
                        const VoxelBrickHeader_t &h = mirror[i];
                        if ((h.flags & 1u) == 0u)
                            continue;
                        if (m_StreamWorld && !m_StreamWorld->BrickHasContent(i))
                            continue;
                        const Vec3 center = h.origin + Vec3(brickSize * 0.5f);
                        const Vec3 toC = center - cameraPos;
                        if (glm::dot(toC, toC) > maxDistSq)
                            continue;
                        bool inside = true;
                        for (const auto &pl : planes) {
                            const float d =
                                pl.x * center.x + pl.y * center.y + pl.z * center.z + pl.w;
                            if (d < -radius) {
                                inside = false;
                                break;
                            }
                        }
                        if (!inside)
                            continue;
                        m_VisibleList.push_back(i);
                    }
                    visibleCount = static_cast<u32>(m_VisibleList.size());
                } else {
                    const auto &mirrorNF = m_World->GetHeaderMirror();
                    for (u32 i = 0; i < brickCount; ++i) {
                        if (m_World->IsBrickHidden(i))
                            continue;
                        if (m_StreamWorld && !m_StreamWorld->BrickHasContent(i))
                            continue;
                        const VoxelBrickHeader_t &h = mirrorNF[i];
                        if ((h.flags & 1u) == 0u)
                            continue;
                        const Vec3 center = h.origin + Vec3(brickSize * 0.5f);
                        const Vec3 toC = center - cameraPos;
                        if (glm::dot(toC, toC) > maxDistSq)
                            continue;
                        m_VisibleList.push_back(i);
                    }
                    visibleCount = static_cast<u32>(m_VisibleList.size());
                }
                const size_t nvis = m_VisibleList.size();
                if (nvis >= 2) {
                    const auto &mirror2 = m_World->GetHeaderMirror();
                    m_SortScratch.clear();
                    m_SortScratch.reserve(nvis);
                    for (const u32 bi : m_VisibleList) {
                        const Vec3 center = mirror2[bi].origin + Vec3(brickSize * 0.5f);
                        const Vec3 toC = center - cameraPos;
                        m_SortScratch.emplace_back(glm::dot(toC, toC), bi);
                    }
                    std::sort(m_SortScratch.begin(), m_SortScratch.end(),
                              [](const auto &a, const auto &b) { return a.first < b.first; });
                    for (size_t k = 0; k < nvis; ++k)
                        m_VisibleList[k] = m_SortScratch[k].second;
                }
                m_CachedViewProj = viewProj;
                m_CachedCameraPos = cameraPos;
                m_CachedBrickCount = brickCount;
                m_CachedHeaderVersion = headerVersion;
                m_CachedUseFrustum = m_bUseFrustum;
                m_VisCacheValid = true;
            } else {
                visibleCount = static_cast<u32>(m_VisibleList.size());
            }
            if (!m_VisibleList.empty())
                visSlot.LoadData(m_VisibleList.data(), sizeof(u32) * m_VisibleList.size());
        }

        VoxelFrameParams_t params{};
        params.viewProj = viewProj;
        params.prevViewProj = prevViewProj;
        params.cameraPos = cameraPos;
        params.brickCount = brickCount;
        params.worldMin = m_World->GetWorldMin();
        params.brickSize = m_World->GetBrickSize();
        params.maxDrawDistance = static_cast<u32>(maxDrawDist);
        params.enableHiZ = 0;
        params.paletteAddr = m_PaletteBuffer->Address();
        params.sunAddr = m_SunBuffer->Address();
        params.giAddr = GetGiSampleAddr();
        params.giEnabled = m_bGiEnabled ? 1u : 0u;
        params.shadowsEnabled = 0;
        params.debugEnabled = m_bDebugEnabled ? 1u : 0u;
        params.useBackface = m_bUseBackface ? 1u : 0u;
        params.useFrustum = m_bUseFrustum ? 1u : 0u;
        params.virtualDim = m_World->GetVirtualDim();
        CBuffer &paramsSlot = *m_FrameParamsRing[flightSlot % kFlightSlots];
        params.cutoutPass = 0u;
        paramsSlot.LoadData(&params, sizeof(params));
        const bool wantCutout = m_HasAnyCutout && m_CutoutPipeline->IsValid();
        CBuffer *cutoutParamsSlot = nullptr;
        if (wantCutout) {
            cutoutParamsSlot = m_CutoutParamsRing[flightSlot % kFlightSlots].get();
            params.cutoutPass = 1u;
            cutoutParamsSlot->LoadData(&params, sizeof(params));
        }

        VoxelFragParams_t frag{};
        frag.sunDir = m_SunDir;
        frag.sunColor = m_SunColor;
        frag.giAddr = GetGiSampleAddr();
        frag.giEnabled = m_bGiEnabled ? 1u : 0u;
        m_FragParams->LoadData(&frag, sizeof(frag));

        VoxelFrameRoot_t root{};
        root.brickBufferAddr = m_World->GetBrickBufferAddr();
        root.headerAddr = m_World->GetHeaderAddr();
        root.pageTableAddr = m_World->GetPageTableAddr();
        root.visibilityAddr = visSlot.Address();
        root.frameAddr = paramsSlot.Address();
        root.faceCacheAddr = m_FaceCache->Address();
        root.tileTableAddr = m_BlockTileTable ? m_BlockTileTable->Address() : 0u;
        root.blockFlagsAddr = m_BlockFlagsTable ? m_BlockFlagsTable->Address() : 0u;
        root.shapeTableAddr = m_BlockShapeTable ? m_BlockShapeTable->Address() : 0u;
        root.uvTableAddr = m_BlockUvTable ? m_BlockUvTable->Address() : 0u;
        root.fragParamsAddr = m_FragParams ? m_FragParams->Address() : 0u;
        root.boxTableAddr = m_BlockBoxTable ? m_BlockBoxTable->Address() : 0u;
        root.spillAddr = m_SpillCache ? m_SpillCache->Address() : 0u;
        root.spillDescAddr = m_SpillDesc ? m_SpillDesc->Address() : 0u;
        VoxelFrameRoot_t cutoutRoot = root;
        if (wantCutout)
            cutoutRoot.frameAddr = cutoutParamsSlot->Address();

        if (m_bDebugEnabled)
            enc.FillBuffer(*m_DebugReadback, 0, sizeof(u32) * 8);
        root.debugAddr = m_DebugReadback->Address();

        {
            VoxelFrameBarriers bar{};
            bar.params = &paramsSlot;
            bar.paramsSize = sizeof(VoxelFrameParams_t);
            bar.vis = &visSlot;
            bar.visSize = sizeof(u32) * (m_VisibleList.empty() ? 1u : visibleCount);
            bar.frag = m_FragParams.get();
            bar.fragSize = sizeof(VoxelFragParams_t);
            bar.headerOrNull = &m_World->GetHeaderBuffer();
            bar.bricksOrNull = &m_World->GetBrickStore();
            bar.faceCacheOrNull = m_FaceCache.get();
            bar.spillOrNull = m_SpillCache.get();
            bar.spillDescOrNull = m_SpillDesc.get();
            bar.debugOrNull = m_DebugReadback.get();
            bar.debug = m_bDebugEnabled;
            enc.BarrierFrame(bar);
        }

        {
            VoxelFrameBarriers cutoutBar{};
            if (wantCutout) {
                cutoutBar.params = cutoutParamsSlot;
                cutoutBar.paramsSize = sizeof(VoxelFrameParams_t);
                enc.BarrierFrame(cutoutBar);
            }
        }

        enc.BeginMeshPass();
        if (m_Timer && m_Timer->Valid())
            m_Timer->Stamp(enc, qslot, 1, true);

        if (!wantCutout) {
            m_Stats.taskGroups = visibleCount;
            m_Stats.taskGroupsOpaque = visibleCount;
            m_Stats.taskGroupsCutout = 0u;
            m_Stats.cutoutSkipped = 1u;
            enc.DrawVoxelMesh(*m_OpaquePipeline, *m_Tiles, &root, sizeof(root), visibleCount);
            enc.EndMeshPass();
        } else {
            m_Stats.taskGroups = visibleCount * 2u;
            m_Stats.taskGroupsOpaque = visibleCount;
            m_Stats.taskGroupsCutout = visibleCount;
            m_Stats.cutoutSkipped = 0u;
            enc.DrawVoxelMesh(*m_OpaquePipeline, *m_Tiles, &root, sizeof(root), visibleCount);
            enc.EndMeshPass();
            {
                VoxelFrameBarriers bar{};
                bar.faceCacheOrNull = m_FaceCache.get();
                bar.spillOrNull = m_SpillCache.get();
                bar.spillDescOrNull = m_SpillDesc.get();
                bar.headerOrNull = &m_World->GetHeaderBuffer();
                enc.BarrierFrame(bar);
            }
            enc.BeginMeshPassResume();
            enc.DrawVoxelMesh(*m_CutoutPipeline, *m_Tiles, &cutoutRoot, sizeof(cutoutRoot),
                              visibleCount);
            enc.EndMeshPass();
        }
        if (m_Timer && m_Timer->Valid())
            m_Timer->Stamp(enc, qslot, 2, false);

        if (m_bDebugEnabled)
            enc.BarrierMeshToHost();

        m_PrevViewProj = viewProj;
    }

    void CVoxelRenderer::StampPost(CVoxelEncoder &enc, u32 flightSlot, u32 subIdx) {
        if (!m_Timer || !m_Timer->Valid() || subIdx >= kQueriesPerSlot)
            return;
        m_Timer->Stamp(enc, flightSlot % kFlightSlots, subIdx, false);
    }

    void CVoxelRenderer::ReadDebugCounters(u32 out[8]) const {
        if (!m_bDebugEnabled || !m_DebugReadback) {
            for (int i = 0; i < 8; ++i)
                out[i] = m_CachedDebug[i];
            return;
        }

        m_DebugReadback->Invalidate(0, sizeof(u32) * 8);
        const auto *src = static_cast<const u32 *>(m_DebugReadback->GetMapped());
        for (int i = 0; i < 8; ++i) {
            out[i] = src[i];
            m_CachedDebug[i] = src[i];
        }
    }
}
