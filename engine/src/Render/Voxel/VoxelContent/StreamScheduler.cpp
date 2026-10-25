#include "StreamImpl.h"

#include <Manro/Core/CpuAffinity.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace Manro {
    Vec3 CVoxelStreamWorld::Init(CVoxelBrickStore &world, const BlockAssetPack_t &pack,
                                 const VoxelStreamDesc_t &desc,
                                 std::unique_ptr<IVoxelContentSource> content) {
        m_Impl->desc = desc;
        m_Impl->pack = &pack;
        auto &I = *m_Impl;
        I.content = std::move(content);

        I.hasSave = I.content != nullptr;
        if (I.hasSave) {
            Vec3 s{};
            if (I.content->ReadSpawn(s))
                I.spawn = s + Vec3(0.f, 1.f, 0.f);
            else
                I.spawn = Vec3(8.f, 80.f, 8.f);
            std::printf("[StreamWorld] save %s spawn=(%.1f,%.1f,%.1f)\n", desc.worldDir.c_str(),
                        I.spawn.x, I.spawn.y, I.spawn.z);
        } else {
            I.spawn = Vec3(8.f, 80.f, 8.f);
            std::printf("[StreamWorld] no content source, flat world\n");
        }

        const int csx = static_cast<int>(std::floor(I.spawn.x / 16.f));
        const int csz = static_cast<int>(std::floor(I.spawn.z / 16.f));
        const int H = kMaxSectionY - kMinSectionY + 1;
        const u32 brickCap = world.GetBrickCapacity();
        int R = std::max(1, desc.radiusSections);
        int maxR = 1;
        if (brickCap > 0) {
            for (int r = 1; r <= 64; ++r) {
                const long long live = static_cast<long long>(2 * r + 1) *
                                       static_cast<long long>(2 * r + 1) * H;
                if (live <= static_cast<long long>(brickCap))
                    maxR = r;
                else
                    break;
            }
            if (R > maxR) {
                std::printf("[StreamWorld] radius %d exceeds brick capacity %u; "
                            "clamping to %d\n",
                            R, brickCap, maxR);
                R = maxR;
            }
        }
        I.desc.radiusSections = R;
        world.SetWorldMin(Vec3(static_cast<float>((csx - R) * 16), -64.f,
                               static_cast<float>((csz - R) * 16)));
        I.minBX = csx - R;
        I.minBZ = csz - R;

        const int wantEvict1 = (2 * (R + 1) + 1) * H * (2 * (R + 1) + 1);
        const int wantEvict2 = (2 * (R + 2) + 1) * H * (2 * (R + 2) + 1);
        const int capacity = brickCap > 0 ? static_cast<int>(brickCap) : 8192;
        if (wantEvict2 <= capacity)
            I.evictRadius = R + 2;
        else if (wantEvict1 <= capacity)
            I.evictRadius = R + 1;
        else
            I.evictRadius = R;

        world.ReserveResident(static_cast<u32>(capacity));
        std::printf("[StreamWorld] streaming R=%d evict=%d save=%d spawn=(%.1f,%.1f,%.1f)\n", R,
                    I.evictRadius, I.hasSave ? 1 : 0, I.spawn.x, I.spawn.y, I.spawn.z);
        {
            const size_t reserveBricks = static_cast<size_t>(capacity);
            I.slots.reserve(reserveBricks * 2);
            I.pending.reserve(128);
            const size_t window =
                    static_cast<size_t>(2 * R + 1) * H * (2 * R + 1);
            I.unfilled.reserve(window);
            I.unfilledScratch.reserve(window);
            I.evictPending.reserve(1024);
            I.occMirror.assign(reserveBricks * 128, 0u);
            I.fluidMirror.assign(reserveBricks * 128, 0u);
            I.stateMirror.assign(reserveBricks * 4096, 0u);
            I.brickContent.assign(reserveBricks, 0u);
            I.streamBrick.assign(reserveBricks, 0u);
            I.lightMirror.assign(reserveBricks * 4096, 0u);
            I.lightDirtyFlag.assign(reserveBricks, 0u);
            I.lightDirtyMask.assign(reserveBricks, 0u);
            I.lightDirtyList.reserve(256);
            {
                const u32 vd = world.GetVirtualDim() != 0u ? world.GetVirtualDim() : 64u;
                I.slotMirror.assign(static_cast<size_t>(vd) * vd * vd, -1);
                I.slotOwner.assign(static_cast<size_t>(vd) * vd * vd, -1);
            }
        }
        {
            const CpuTopology_t topo = QueryCpuTopology();
            int numWorkers = 1;
            if (topo.IsHybrid() && !topo.m_EfficiencyCores.empty())
                numWorkers = std::min<int>(
                    4, static_cast<int>(topo.m_EfficiencyCores.size()));
            numWorkers = std::max(1, numWorkers);
            I.desc.fillBudgetPerUpdate = 6 * numWorkers;
            I.maxPending = static_cast<size_t>(48 * numWorkers);
            I.pending.reserve(I.maxPending * 2);
            std::printf("[StreamWorld] parse workers=%d light workers=%d fillBudget=%d "
                        "maxPending=%zu\n",
                        numWorkers, numWorkers, I.desc.fillBudgetPerUpdate, I.maxPending);
            I.workerStates.reserve(static_cast<size_t>(numWorkers));
            I.workers.reserve(static_cast<size_t>(numWorkers));
            I.lightWorkers.reserve(static_cast<size_t>(numWorkers));
            for (int w = 0; w < numWorkers; ++w) {
                I.workerStates.push_back(std::make_unique<Impl::WorkerState>());
                I.workers.emplace_back(&Impl::ParseLoop, &I,
                                       static_cast<size_t>(w));
                I.lightWorkers.emplace_back(&Impl::LightLoop, &I,
                                            static_cast<size_t>(w));
            }
        }
        return I.spawn;
    }

    void CVoxelStreamWorld::Impl::ReleaseChunk(CVoxelBrickStore &world, i64 key) {
        std::unique_lock lk(m_MirrorMutex);
        pending.erase(key);
        const auto sit = slots.find(key);
        if (sit == slots.end())
            return;
        const i32 b = sit->second.brickIdx;
        if (b >= 0 && world.EvictBrick(static_cast<u32>(b))) {
            if (static_cast<size_t>(b) < brickContent.size())
                brickContent[static_cast<size_t>(b)] = 0u;
            if (static_cast<size_t>(b) < streamBrick.size())
                streamBrick[static_cast<size_t>(b)] = 0u;
            if (static_cast<size_t>(b) < lightDirtyFlag.size())
                lightDirtyFlag[static_cast<size_t>(b)] = 0u;
            if (static_cast<size_t>(b) < lightDirtyMask.size())
                lightDirtyMask[static_cast<size_t>(b)] = 0u;
        }
        int sx = 0, sy = 0, sz = 0;
        DecodeKey(key, sx, sy, sz);
        const u32 slot = SlotFor(sx, sy, sz);
        if (slot < slotMirror.size())
            slotMirror[slot] = -1;
        if (slot < slotOwner.size() && slotOwner[slot] == key)
            slotOwner[slot] = -1;
        slots.erase(sit);
    }

    void CVoxelStreamWorld::Impl::MarkLightDirty(i64 key, u32 brick, u32 faceMask) {
        if (brick >= lightDirtyFlag.size())
            return;
        const u32 mask = faceMask & 0x3Fu;
        if (mask == 0u)
            return;
        if (lightDirtyFlag[brick] != 0u) {
            if (brick < lightDirtyMask.size())
                lightDirtyMask[brick] =
                        static_cast<u8>(lightDirtyMask[brick] | mask);
            return;
        }
        lightDirtyFlag[brick] = 1u;
        if (brick < lightDirtyMask.size())
            lightDirtyMask[brick] = static_cast<u8>(mask);
        lightDirtyList.push_back({key, brick});
    }

    void CVoxelStreamWorld::Impl::ProcessLightDirty(int budget) {
        int processed = 0;
        while (!lightDirtyList.empty() && processed < budget) {
            const LightDirty_t d = lightDirtyList.back();
            lightDirtyList.pop_back();
            if (d.brick >= lightDirtyFlag.size())
                continue;
            if (lightDirtyFlag[d.brick] == 0u)
                continue;
            const auto sit = slots.find(d.key);
            if (sit == slots.end() ||
                sit->second.brickIdx != static_cast<i32>(d.brick) ||
                !sit->second.filled) {
                continue;
            }
            lightDirtyFlag[d.brick] = 0u;
            const u32 faceMask =
                    d.brick < lightDirtyMask.size()
                        ? static_cast<u32>(lightDirtyMask[d.brick])
                        : 0x3Fu;
            if (d.brick < lightDirtyMask.size())
                lightDirtyMask[d.brick] = 0u;
            int sx = 0, sy = 0, sz = 0;
            DecodeKey(d.key, sx, sy, sz);
            {
                std::shared_lock mlk(m_MirrorMutex);
                std::unique_lock llk(m_LightMutex);
                BorderPullLight(d.brick, sx, sy, sz, faceMask);
            }
            {
                std::unique_lock mlk(m_MirrorMutex);
                bool dup = false;
                for (const auto &q: editRestage) {
                    if (q.key == d.key) {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    editRestage.push_back({d.key, d.brick});
            }
            ++processed;
        }
    }

    int CVoxelStreamWorld::Update(CVoxelBrickStore &world, const Vec3 &cameraPos, u32 flightSlot) {
        auto &I = *m_Impl;
        using Clock = std::chrono::steady_clock;
        const auto tAll = Clock::now();
        auto tPhase = tAll;
        auto &ST = I.lastStats;
        ST = VoxelStreamStats_t{};
        I.lightMsAccum = 0.0;
        I.lightBrickCount = 0;
        I.lightVisitCount = 0;
        I.lightQueuePeak = 0;
        const auto snapMs = [](const auto &t0) {
            return std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
        };
        const int R = I.desc.radiusSections;
        const auto secOf = [](float v) { return static_cast<int>(std::floor(v / 16.f)); };
        const int pcx = secOf(cameraPos.x);
        const int pcy = secOf(cameraPos.y);
        const int pcz = secOf(cameraPos.z);

        if (!I.hasCenter || pcx != I.centerCx || pcz != I.centerCz) {
            I.centerCx = pcx;
            I.centerCz = pcz;
            I.hasCenter = true;
            const int E = I.evictRadius;
            const int W = 2 * R + 1;
            const int H = kMaxSectionY - kMinSectionY + 1;
            thread_local std::vector<u8> cell;
            cell.assign(static_cast<size_t>(W) * H * W, 0u);
            I.evictPending.clear();
            for (const auto &kv: I.slots) {
                int sx, sy, sz;
                Impl::DecodeKey(kv.first, sx, sy, sz);
                if (std::max(std::abs(sx - pcx), std::abs(sz - pcz)) > E) {
                    I.evictPending.push_back(kv.first);
                    continue;
                }
                const int lx = sx - (pcx - R);
                const int ly = sy - kMinSectionY;
                const int lz = sz - (pcz - R);
                if (static_cast<unsigned>(lx) >= static_cast<unsigned>(W) ||
                    ly < 0 || ly >= H ||
                    static_cast<unsigned>(lz) >= static_cast<unsigned>(W))
                    continue;
                cell[(static_cast<size_t>(lz) * H + ly) * W + lx] =
                        kv.second.filled ? 2u : 1u;
            }
            I.unfilledScratch.clear();
            for (int lz = 0; lz < W; ++lz) {
                for (int ly = 0; ly < H; ++ly) {
                    for (int lx = 0; lx < W; ++lx) {
                        if (cell[(static_cast<size_t>(lz) * H + ly) * W + lx] == 2u)
                            continue;
                        const int sx = pcx - R + lx;
                        const int sy = kMinSectionY + ly;
                        const int sz = pcz - R + lz;
                        const float dx = static_cast<float>(sx - pcx);
                        const float dy = static_cast<float>(sy - pcy);
                        const float dz = static_cast<float>(sz - pcz);
                        I.unfilledScratch.emplace_back(
                            dx * dx + dy * dy + dz * dz, Impl::Key(sx, sy, sz));
                    }
                }
            }

            std::sort(I.unfilledScratch.begin(), I.unfilledScratch.end(),
                      [](const auto &a, const auto &b) { return a.first < b.first; });
            I.unfilled.resize(I.unfilledScratch.size());
            for (size_t k = 0; k < I.unfilledScratch.size(); ++k)
                I.unfilled[k] = I.unfilledScratch[k].second;
        }
        ST.rebuildMs = snapMs(tPhase);
        tPhase = Clock::now();

        {
            const int E = I.evictRadius;
            const int ccx = I.centerCx, ccz = I.centerCz;
            int evicted = 0, scanned = 0;
            const int kMaxScans = Impl::kEvictPerUpdate * 4;
            while (!I.evictPending.empty() && evicted < Impl::kEvictPerUpdate &&
                   scanned < kMaxScans) {
                const i64 key = I.evictPending.back();
                I.evictPending.pop_back();
                ++scanned;
                const auto it = I.slots.find(key);
                if (it == I.slots.end())
                    continue;
                int sx, sy, sz;
                Impl::DecodeKey(key, sx, sy, sz);
                if (std::max(std::abs(sx - ccx), std::abs(sz - ccz)) <= E)
                    continue;
                const i32 b = it->second.brickIdx;
                if (b >= 0 && world.EvictBrick(static_cast<u32>(b))) {
                    if (static_cast<size_t>(b) < I.brickContent.size())
                        I.brickContent[static_cast<size_t>(b)] = 0u;
                    if (static_cast<size_t>(b) < I.streamBrick.size())
                        I.streamBrick[static_cast<size_t>(b)] = 0u;
                    if (static_cast<size_t>(b) < I.lightDirtyFlag.size())
                        I.lightDirtyFlag[static_cast<size_t>(b)] = 0u;
                    if (static_cast<size_t>(b) < I.lightDirtyMask.size())
                        I.lightDirtyMask[static_cast<size_t>(b)] = 0u;
                }
                {
                    const u32 slot = I.SlotFor(sx, sy, sz);
                    std::unique_lock lk(I.m_MirrorMutex);
                    if (slot < I.slotMirror.size())
                        I.slotMirror[slot] = -1;
                    if (slot < I.slotOwner.size() && I.slotOwner[slot] == key)
                        I.slotOwner[slot] = -1;
                    I.slots.erase(it);
                }
                for (int a = 0; a < 6; ++a) {
                    const auto &o = Impl::kHaloDir[a];
                    const i64 nkey = Impl::Key(sx + o[0], sy + o[1], sz + o[2]);
                    const auto nit = I.slots.find(nkey);
                    if (nit != I.slots.end() && nit->second.filled &&
                        nit->second.brickIdx >= 0) {
                        I.MarkLightDirty(nkey,
                                         static_cast<u32>(nit->second.brickIdx),
                                         1u << (a ^ 1));
                    }
                }
                ++evicted;
            }
        }
        ST.evictMs = snapMs(tPhase);
        tPhase = Clock::now();

        {
            bool anyDone = false;
            for (const auto &s: I.workerStates) {
                if (!s->done.Empty()) {
                    anyDone = true;
                    break;
                }
            }
            if (I.unfilled.empty() && I.evictPending.empty() && I.pending.empty() &&
                I.deferQueue.empty() && I.editRestage.empty() &&
                I.lightDirtyList.empty() && !anyDone) {
                ST.totalMs = snapMs(tAll);
                ST.deferredBricks = static_cast<u32>(I.deferQueue.size());
                ST.lightMs = static_cast<float>(I.lightMsAccum);
                ST.lightBricks = I.lightBrickCount;
                ST.lightDirtyBricks = static_cast<u32>(I.lightDirtyList.size());
                ST.lightVisits = I.lightVisitCount;
                ST.lightQueuePeak = static_cast<u32>(I.lightQueuePeak);
                return 0;
            }
        }

        const int budget = I.desc.fillBudgetPerUpdate;

        thread_local std::vector<u16> batchMats;
        thread_local std::vector<u32> batchOcc;
        thread_local std::vector<u32> batchFluid;
        thread_local std::vector<u32> batchGpuOcc;
        thread_local std::vector<u8> batchLight;
        thread_local std::vector<i64> filledKeys;
        thread_local std::vector<i64> freshKeys;
        thread_local std::vector<u32> batchIdx;
        thread_local std::vector<u8> batchOpaque;
        thread_local std::vector<u8> batchHasEmis;
        batchMats.resize(static_cast<size_t>(budget) * 4096);
        batchOcc.resize(static_cast<size_t>(budget) * 128);
        batchFluid.resize(static_cast<size_t>(budget) * 128);
        batchGpuOcc.resize(static_cast<size_t>(budget) * 128);
        batchLight.resize(static_cast<size_t>(budget) * 4096);
        batchIdx.clear();
        batchOpaque.clear();
        batchHasEmis.clear();
        filledKeys.clear();
        freshKeys.clear();

        if (!I.unfilled.empty()) {
            size_t inFlight = I.pending.size();
            for (const auto &s: I.workerStates)
                inFlight += s->jobs.Size() + s->parsed.Size() + s->done.Size();
            if (inFlight < I.maxPending) {
                int enqueued = 0;
                const size_t numW = I.workerStates.size();
                for (const i64 key: I.unfilled) {
                    if (enqueued >= budget || inFlight >= I.maxPending)
                        break;
                    int sx, sy, sz;
                    Impl::DecodeKey(key, sx, sy, sz);
                    auto sit = I.slots.find(key);
                    u32 bidx = ~0u;
                    if (sit == I.slots.end()) {
                        const u32 slot = I.SlotFor(sx, sy, sz);
                        if (slot < I.slotOwner.size()) {
                            const i64 oldKey = I.slotOwner[slot];
                            if (oldKey >= 0 && oldKey != key)
                                I.ReleaseChunk(world, oldKey);
                        }
                        const i32 nb = world.AllocateBrickSlot(
                            slot, Impl::OriginFor(sx, sy, sz));
                        if (nb < 0)
                            break;
                        bidx = static_cast<u32>(nb);
                        {
                            std::unique_lock lk(I.m_MirrorMutex);
                            sit = I.slots.emplace(key, Impl::SlotInfo{nb, false}).first;
                            if (slot < I.slotMirror.size())
                                I.slotMirror[slot] = -1;
                            if (slot < I.slotOwner.size())
                                I.slotOwner[slot] = key;
                        }
                        if (static_cast<size_t>(bidx) < I.streamBrick.size())
                            I.streamBrick[static_cast<size_t>(bidx)] = 1u;
                        if (static_cast<size_t>(bidx) < I.brickContent.size())
                            I.brickContent[static_cast<size_t>(bidx)] = 0u;
                        world.BumpVisibilityVersion();
                    } else if (sit->second.filled || sit->second.brickIdx < 0) {
                        continue;
                    } else {
                        bidx = static_cast<u32>(sit->second.brickIdx);
                    }
                    if (I.pending.find(key) != I.pending.end())
                        continue;
                    Impl::ParseJob job;
                    job.key = key;
                    job.sx = sx;
                    job.sy = sy;
                    job.sz = sz;
                    job.brickIdx = bidx;
                    const u64 colHash =
                            static_cast<u64>(static_cast<u32>(sx)) * 73856093ull ^
                            static_cast<u64>(static_cast<u32>(sz)) * 19349663ull;
                    bool pushed = false;
                    for (size_t t = 0; t < numW; ++t) {
                        if (I.workerStates[(colHash + t) %
                                           static_cast<u64>(numW)]
                            ->jobs.Push(std::move(job))) {
                            pushed = true;
                            break;
                        }
                    }
                    if (!pushed)
                        break;
                    I.pending.insert(key);
                    ++enqueued;
                    ++inFlight;
                }
                if (enqueued > 0)
                    I.queueCv.notify_all();
            }
        }
        {
            size_t parsedTotal = 0;
            for (const auto &s: I.workerStates)
                parsedTotal += s->parsed.Size();
            I.lightQueuePeak = std::max(I.lightQueuePeak, parsedTotal);
        }
        ST.enqueueMs = snapMs(tPhase);
        tPhase = Clock::now();

        {
            int drained = 0;
            while (drained < budget) {
                bool progress = false;
                for (const auto &s: I.workerStates) {
                    if (drained >= budget)
                        break;
                    Impl::ParseDone d;
                    if (!s->done.Pop(d))
                        continue;
                    progress = true;
                    const auto sit = I.slots.find(d.key);
                    const bool stale =
                    (sit == I.slots.end() ||
                     sit->second.brickIdx != static_cast<i32>(d.brickIdx) ||
                     sit->second.filled);
                    if (stale) {
                        I.pending.erase(d.key);
                        continue;
                    }
                    const size_t k = batchIdx.size();
                    std::memcpy(batchMats.data() + k * 4096, d.mats.data(),
                                sizeof(u16) * 4096);
                    std::memcpy(batchOcc.data() + k * 128, d.occ.data(),
                                sizeof(u32) * 128);
                    std::memcpy(batchFluid.data() + k * 128, d.fluid.data(),
                                sizeof(u32) * 128);
                    std::memcpy(batchLight.data() + k * 4096, d.light.data(), 4096);
                    batchIdx.push_back(d.brickIdx);
                    batchOpaque.push_back(d.opaque ? 1u : 0u);
                    batchHasEmis.push_back(d.hasEmission ? 1u : 0u);
                    I.lightVisitCount += d.lightVisits;
                    freshKeys.push_back(d.key);
                    ++drained;
                }
                if (!progress)
                    break;
            }
        }
        ST.drainMs = snapMs(tPhase);
        tPhase = Clock::now();
        thread_local std::vector<u16> stageMats;
        thread_local std::vector<u32> stageOcc;
        thread_local std::vector<u32> stageIdx;
        thread_local std::vector<u8> stageLight;
        const auto commitOne = [&](u32 brickIdx, const u32 *occ, const u32 *fluid,
                                   const u16 *mats, const u8 *light, u8 opaque, u8 hasEmis,
                                   i64 key) {
            const size_t bi = static_cast<size_t>(brickIdx);
            u32 any = 0u;
            for (int w = 0; w < 128; ++w)
                any |= occ[w] | fluid[w];
            int dsx = 0, dsy = 0, dsz = 0;
            Impl::DecodeKey(key, dsx, dsy, dsz);
            const u32 slot = I.SlotFor(dsx, dsy, dsz);
            const u8 newContent = (any != 0u) ? 1u : 0u;
            bool visibilityChanged = false;
            {
                std::unique_lock mlk(I.m_MirrorMutex);
                std::unique_lock llk(I.m_LightMutex);
                const size_t need = (bi + 1) * 128;
                if (I.occMirror.size() < need) {
                    I.occMirror.resize(need, 0u);
                    I.fluidMirror.resize(need, 0u);
                }
                std::memcpy(I.occMirror.data() + bi * 128, occ, sizeof(u32) * 128);
                std::memcpy(I.fluidMirror.data() + bi * 128, fluid, sizeof(u32) * 128);

                const size_t sneed = (bi + 1) * 4096;
                if (I.stateMirror.size() < sneed)
                    I.stateMirror.resize(sneed, 0u);
                std::memcpy(I.stateMirror.data() + bi * 4096, mats, sizeof(u16) * 4096);

                const size_t lneed = (bi + 1) * 4096;
                if (I.lightMirror.size() < lneed)
                    I.lightMirror.resize(lneed, 0u);
                std::memcpy(I.lightMirror.data() + bi * 4096, light, 4096);

                const auto sit = I.slots.find(key);
                if (sit != I.slots.end() &&
                    sit->second.brickIdx == static_cast<i32>(brickIdx) &&
                    !sit->second.filled) {
                    sit->second.filled = true;
                    filledKeys.push_back(key);
                }
                I.pending.erase(key);
                if (slot < I.slotMirror.size())
                    I.slotMirror[slot] = static_cast<i32>(brickIdx);
                if (bi < I.brickContent.size() && I.brickContent[bi] != newContent) {
                    I.brickContent[bi] = newContent;
                    visibilityChanged = true;
                }
                if (bi < I.streamBrick.size() && I.streamBrick[bi] == 0u) {
                    I.streamBrick[bi] = 1u;
                    visibilityChanged = true;
                }
            }

            world.SetBrickOpaqueFull(brickIdx, opaque != 0u);
            world.MarkBrickAndNeighborsDirty(brickIdx);
            if (visibilityChanged)
                world.BumpVisibilityVersion();

            const auto t0 = std::chrono::steady_clock::now();
            std::vector<Impl::EditRestage_t> glowRestage;
            {
                std::shared_lock mlk(I.m_MirrorMutex);
                std::unique_lock llk(I.m_LightMutex);
                if (opaque == 0u || hasEmis != 0u)
                    I.BorderPullLight(brickIdx, dsx, dsy, dsz);
                if (hasEmis != 0u)
                    I.GlowPushToNeighbours(brickIdx, dsx, dsy, dsz, glowRestage);
            }
            if (!glowRestage.empty()) {
                std::unique_lock mlk(I.m_MirrorMutex);
                for (const auto &e: glowRestage) {
                    bool dup = false;
                    for (const auto &q: I.editRestage) {
                        if (q.key == e.key) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup)
                        I.editRestage.push_back(e);
                }
            }
            I.lightMsAccum += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0)
                    .count();
            ++I.lightBrickCount;
        };

        if (!I.deferQueue.empty()) {
            {
                size_t kept = 0;
                for (size_t r = 0; r < I.deferQueue.size(); ++r) {
                    auto &df = I.deferQueue[r];
                    const auto sit = I.slots.find(df.key);
                    if (sit == I.slots.end() ||
                        sit->second.brickIdx != static_cast<i32>(df.brickIdx) ||
                        sit->second.filled) {
                        I.pending.erase(df.key);
                        continue;
                    }
                    if (kept != r)
                        I.deferQueue[kept] = std::move(df);
                    ++kept;
                }
                I.deferQueue.resize(kept);
            }
            const u32 free = world.GetStageFree(flightSlot);
            constexpr size_t kDeferredStagePerUpdate = 32;
            const size_t n = std::min<size_t>(
                std::min<size_t>(I.deferQueue.size(), free),
                kDeferredStagePerUpdate);
            if (n > 0) {
                stageMats.resize(n * 4096);
                stageOcc.resize(n * 128);
                stageIdx.resize(n);
                stageLight.resize(n * 4096);
                for (size_t k = 0; k < n; ++k) {
                    const auto &df = I.deferQueue[k];
                    std::memcpy(stageMats.data() + k * 4096, df.mats.data(),
                                sizeof(u16) * 4096);
                    for (int w = 0; w < 128; ++w)
                        stageOcc[k * 128 + w] = df.occ[w] | df.fluid[w];
                    std::memcpy(stageLight.data() + k * 4096, df.light.data(), 4096);
                    stageIdx[k] = df.brickIdx;
                }
                if (world.StageBrickBatch(flightSlot, stageIdx.data(), stageMats.data(),
                                          stageOcc.data(), stageLight.data(),
                                          static_cast<u32>(n))) {
                    for (size_t k = 0; k < n; ++k) {
                        const auto &df = I.deferQueue[k];
                        commitOne(df.brickIdx, df.occ.data(), df.fluid.data(), df.mats.data(),
                                  df.light.data(), df.opaque, df.hasEmis, df.key);
                    }
                    I.deferQueue.erase(I.deferQueue.begin(),
                                       I.deferQueue.begin() + static_cast<ptrdiff_t>(n));
                }
            }
        }

        if (!I.editRestage.empty()) {
            std::unique_lock lk(I.m_MirrorMutex);
            {
                size_t kept = 0;
                for (size_t r = 0; r < I.editRestage.size(); ++r) {
                    const auto &e = I.editRestage[r];
                    const auto sit = I.slots.find(e.key);
                    if (sit == I.slots.end() ||
                        sit->second.brickIdx != static_cast<i32>(e.brick) ||
                        !sit->second.filled)
                        continue;
                    if (kept != r)
                        I.editRestage[kept] = e;
                    ++kept;
                }
                I.editRestage.resize(kept);
            }
            const u32 free = world.GetStageFree(flightSlot);
            constexpr size_t kEditRestagePerUpdate = 32;
            const size_t n = std::min<size_t>(
                std::min<size_t>(I.editRestage.size(), free),
                kEditRestagePerUpdate);
            if (n > 0) {
                stageMats.resize(n * 4096);
                stageOcc.resize(n * 128);
                stageIdx.resize(n);
                stageLight.resize(n * 4096);
                for (size_t k = 0; k < n; ++k) {
                    const u32 b = I.editRestage[k].brick;
                    std::memcpy(stageMats.data() + k * 4096,
                                I.stateMirror.data() + static_cast<size_t>(b) * 4096,
                                sizeof(u16) * 4096);
                    for (int w = 0; w < 128; ++w)
                        stageOcc[k * 128 + w] =
                                I.occMirror[static_cast<size_t>(b) * 128 + w] |
                                I.fluidMirror[static_cast<size_t>(b) * 128 + w];
                    std::memcpy(stageLight.data() + k * 4096,
                                I.lightMirror.data() + static_cast<size_t>(b) * 4096,
                                4096);
                    stageIdx[k] = b;
                }
                if (world.StageBrickBatch(flightSlot, stageIdx.data(), stageMats.data(),
                                          stageOcc.data(), stageLight.data(),
                                          static_cast<u32>(n))) {
                    for (size_t k = 0; k < n; ++k)
                        world.MarkBrickDirty(stageIdx[k]);
                    I.editRestage.erase(I.editRestage.begin(),
                                        I.editRestage.begin() +
                                        static_cast<ptrdiff_t>(n));
                }
            }
        }

        if (!batchIdx.empty()) {
            for (size_t k = 0; k < batchIdx.size(); ++k) {
                const u32 *occ = batchOcc.data() + k * 128;
                const u32 *fld = batchFluid.data() + k * 128;
                u32 *gpu = batchGpuOcc.data() + k * 128;
                for (int w = 0; w < 128; ++w)
                    gpu[w] = occ[w] | fld[w];
            }
            const u32 free = world.GetStageFree(flightSlot);
            const u32 nStage =
                    std::min<u32>(static_cast<u32>(batchIdx.size()), free);
            u32 nStaged = 0;
            if (nStage > 0 &&
                world.StageBrickBatch(flightSlot, batchIdx.data(), batchMats.data(),
                                      batchGpuOcc.data(), batchLight.data(), nStage)) {
                for (u32 k = 0; k < nStage; ++k) {
                    commitOne(batchIdx[k], batchOcc.data() + static_cast<size_t>(k) * 128,
                              batchFluid.data() + static_cast<size_t>(k) * 128,
                              batchMats.data() + static_cast<size_t>(k) * 4096,
                              batchLight.data() + static_cast<size_t>(k) * 4096,
                              batchOpaque[k], batchHasEmis[k], freshKeys[k]);
                }
                nStaged = nStage;
            }
            for (size_t k = nStaged; k < batchIdx.size(); ++k) {
                if (I.deferQueue.size() >= Impl::kMaxDeferred) {
                    ++I.totalDeferredDropped;
                    std::printf("[StreamWorld] deferral cap hit, dropping oldest (%zu queued)\n",
                                I.deferQueue.size());
                    I.ReleaseChunk(world, I.deferQueue.front().key);
                    I.deferQueue.erase(I.deferQueue.begin());
                }
                Impl::DeferredUpload_t df;
                df.key = freshKeys[k];
                df.brickIdx = batchIdx[k];
                df.opaque = batchOpaque[k];
                df.hasEmis = batchHasEmis[k];
                df.mats.assign(batchMats.begin() + static_cast<ptrdiff_t>(k * 4096),
                               batchMats.begin() + static_cast<ptrdiff_t>((k + 1) * 4096));
                df.occ.assign(batchOcc.begin() + static_cast<ptrdiff_t>(k * 128),
                              batchOcc.begin() + static_cast<ptrdiff_t>((k + 1) * 128));
                df.fluid.assign(batchFluid.begin() + static_cast<ptrdiff_t>(k * 128),
                                batchFluid.begin() + static_cast<ptrdiff_t>((k + 1) * 128));
                df.light.assign(batchLight.begin() + static_cast<ptrdiff_t>(k * 4096),
                                batchLight.begin() + static_cast<ptrdiff_t>((k + 1) * 4096));
                I.deferQueue.push_back(std::move(df));
            }
        }
        if (!filledKeys.empty()) {
            std::sort(filledKeys.begin(), filledKeys.end());
            size_t w = 0;
            for (size_t r = 0; r < I.unfilled.size(); ++r) {
                if (!std::binary_search(filledKeys.begin(), filledKeys.end(), I.unfilled[r]))
                    I.unfilled[w++] = I.unfilled[r];
            }
            I.unfilled.resize(w);
        }

        I.ProcessLightDirty(8);

        ST.commitMs = snapMs(tPhase);
        ST.totalMs = snapMs(tAll);
        ST.lightMs = static_cast<float>(I.lightMsAccum);
        ST.lightBricks = I.lightBrickCount;
        ST.lightDirtyBricks = static_cast<u32>(I.lightDirtyList.size());
        ST.lightVisits = I.lightVisitCount;
        ST.lightQueuePeak = static_cast<u32>(I.lightQueuePeak);
        ++I.updateCount;
        if ((I.updateCount % 600u) == 0u || ST.lightMs > 5.0f) {
            std::printf("[StreamWorld][light] update=%llu bricks=%u visits=%llu peakQ=%zu "
                        "dirty=%zu ms=%.2f (commit %.2fms)\n",
                        static_cast<unsigned long long>(I.updateCount), I.lightBrickCount,
                        static_cast<unsigned long long>(I.lightVisitCount), I.lightQueuePeak,
                        I.lightDirtyList.size(), I.lightMsAccum, ST.commitMs);
        }
        const auto up = world.TakeUploadStats();
        I.totalBlockingUploads += up.blockingUploads;
        ST.blockingUploads = I.totalBlockingUploads;
        ST.deferredBricks = static_cast<u32>(I.deferQueue.size());
        ST.deferredDropped = I.totalDeferredDropped;
        for (u32 s = 0; s < 3; ++s)
            ST.stageHighWater[s] = up.highWater[s];
        ST.midFrameBinds = static_cast<u32>(world.TakeBindSubmitCount());
        return static_cast<int>(I.unfilled.size());
    }
}
