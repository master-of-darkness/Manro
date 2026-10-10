#include "VoxelStreamWorld.h"
#include "VoxelAnvilReader.h"
#include "VoxelBlockAssets.h"
#include "VoxelWorld.h"

#include <Manro/Core/CpuAffinity.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Manro {
    namespace {
        constexpr int kMinSectionY = -4;
        constexpr int kMaxSectionY = 19;

        template <typename T, size_t kCap>
        struct SpscQueue {
            static_assert((kCap & (kCap - 1)) == 0, "capacity must be a power of two");
            std::array<T, kCap> m_Buf{};
            alignas(64) std::atomic<size_t> m_Head{0};
            alignas(64) std::atomic<size_t> m_Tail{0};

            bool Push(T &&v) {
                const size_t tail = m_Tail.load(std::memory_order_relaxed);
                if (tail - m_Head.load(std::memory_order_acquire) >= kCap)
                    return false;
                m_Buf[tail & (kCap - 1)] = std::move(v);
                m_Tail.store(tail + 1, std::memory_order_release);
                return true;
            }

            bool Pop(T &out) {
                const size_t head = m_Head.load(std::memory_order_relaxed);
                if (m_Tail.load(std::memory_order_acquire) == head)
                    return false;
                out = std::move(m_Buf[head & (kCap - 1)]);
                m_Head.store(head + 1, std::memory_order_release);
                return true;
            }

            [[nodiscard]] bool Empty() const {
                return m_Tail.load(std::memory_order_acquire) ==
                       m_Head.load(std::memory_order_acquire);
            }

            [[nodiscard]] size_t Size() const {
                return m_Tail.load(std::memory_order_acquire) -
                       m_Head.load(std::memory_order_acquire);
            }
        };
    }

    struct CVoxelStreamWorld::Impl {
        VoxelStreamDesc_t desc{};
        const BlockAssetPack_t *pack{nullptr};
        CAnvilWorldReader reader;
        bool hasSave{false};

        struct SlotInfo {
            i32 brickIdx{-1};
            bool filled{false};
        };
        std::unordered_map<i64, SlotInfo> slots;

        std::vector<i64> unfilled;
        std::vector<std::pair<float, i64>> unfilledScratch;

        struct ParseJob {
            i64 key{-1};
            int sx{0}, sy{0}, sz{0};
            u32 brickIdx{~0u};
        };
        struct ParseDone {
            i64 key{-1};
            u32 brickIdx{~0u};
            std::vector<u16> mats;
            std::vector<u32> occ;
            std::vector<u32> fluid;
            bool opaque{false};
        };
        struct WorkerState {
            SpscQueue<ParseJob, 64> jobs;
            SpscQueue<ParseDone, 64> done;
        };
        std::vector<std::unique_ptr<WorkerState>> workerStates;
        std::vector<std::thread> workers;
        std::mutex sleepMutex;
        std::condition_variable queueCv;
        std::unordered_set<i64> pending;
        std::atomic<bool> stopWorker{false};
        size_t maxPending{64};

        struct DeferredUpload_t {
            i64 key{-1};
            u32 brickIdx{~0u};
            u8 opaque{0};
            std::vector<u16> mats;
            std::vector<u32> occ;
            std::vector<u32> fluid;
        };
        std::vector<DeferredUpload_t> deferQueue;
        static constexpr size_t kMaxDeferred = 4096;

        VoxelStreamStats_t lastStats{};
        u64 totalBlockingUploads{0};
        u64 totalDeferredDropped{0};

        void ParseLoop(size_t wi) {
            const CpuTopology_t topo = QueryCpuTopology();
            if (topo.IsHybrid() && !topo.m_EfficiencyCores.empty())
                SetThreadAffinity(topo.m_EfficiencyCores);
            CAnvilWorldReader localReader;
            const bool localHasSave =
                !desc.worldDir.empty() && localReader.Open(desc.worldDir);
            std::vector<u32> palLocal;
            AnvilSectionStates saveSec{};
            WorkerState &st = *workerStates[wi];
            for (;;) {
                ParseJob job;
                if (!st.jobs.Pop(job)) {
                    std::unique_lock<std::mutex> lk(sleepMutex);
                    queueCv.wait(lk, [&] {
                        return stopWorker.load(std::memory_order_acquire) ||
                               !st.jobs.Empty();
                    });
                    if (stopWorker.load(std::memory_order_acquire))
                        return;
                    continue;
                }
                ParseDone out;
                out.key = job.key;
                out.brickIdx = job.brickIdx;
                out.mats.assign(4096, 0);
                out.occ.assign(128, 0u);
                out.fluid.assign(128, 0u);
                bool allOpaque = true;
                palLocal.clear();
                bool haveData = false;
                if (localHasSave &&
                    localReader.ReadSectionStates(job.sx, job.sy, job.sz, saveSec) &&
                    saveSec.present) {
                    haveData = true;
                    palLocal.reserve(saveSec.palette.size());
                    for (const FullState &fs : saveSec.palette) {
                        const auto it =
                            pack->stateByKey.find(MakeStateKey(fs.name, fs.props));
                        palLocal.push_back(it != pack->stateByKey.end()
                                                ? it->second
                                                : pack->fallbackState);
                    }
                }
                for (int vi = 0; vi < 4096; ++vi) {
                    const u32 st = haveData ? palLocal[saveSec.pal[vi]] : 0u;
                    const bool valid = static_cast<size_t>(st) < pack->states.size();
                    const u32 flags = valid ? pack->states[static_cast<size_t>(st)].flags : 0u;
                    const bool skip = !valid || ((flags & kBlockFlagSkip) != 0);
                    out.mats[static_cast<size_t>(vi)] = skip ? 0 : static_cast<u16>(st);
                    if (!skip) {
                        const bool fluid = static_cast<size_t>(st) < pack->fluid.size() &&
                                           pack->fluid[static_cast<size_t>(st)] != 0u;
                        if (fluid)
                            out.fluid[vi >> 5u] |= (1u << (vi & 31u));
                        else
                            out.occ[vi >> 5u] |= (1u << (vi & 31u));
                    }
                    if (skip || (flags & kBlockFlagOpaque) == 0u)
                        allOpaque = false;
                }
                out.opaque = allOpaque;
                while (!st.done.Push(std::move(out))) {
                    if (stopWorker.load(std::memory_order_acquire))
                        return;
                    std::this_thread::yield();
                }
            }
        }

        ~Impl() {
            stopWorker.store(true, std::memory_order_release);
            queueCv.notify_all();
            for (auto &t : workers) {
                if (t.joinable())
                    t.join();
            }
        }

        int centerCx{0}, centerCz{0};
        bool hasCenter{false};
        int evictRadius{7};
        std::vector<i64> evictPending;
        static constexpr int kEvictPerUpdate = 64;

        int minBX{0}, minBZ{0};

        std::vector<u32> occMirror;
        std::vector<u32> fluidMirror;

        std::vector<u16> stateMirror;
        std::vector<u8> brickContent;
        Vec3 spawn{8.f, 80.f, 8.f};

        static i64 Key(int sx, int sy, int sz) {
            return (static_cast<i64>(sx + 32768) << 42) | (static_cast<i64>(sy + 32768) << 21) |
                   static_cast<i64>(sz + 32768);
        }

        static void DecodeKey(i64 key, int &sx, int &sy, int &sz) {
            sx = static_cast<int>((key >> 42) & 0x1FFFFF) - 32768;
            sy = static_cast<int>((key >> 21) & 0x1FFFFF) - 32768;
            sz = static_cast<int>(key & 0x1FFFFF) - 32768;
        }

        u32 SlotFor(int sx, int sy, int sz) const {
            constexpr int D = 64;
            int bx = (sx - minBX) % D;
            int by = (sy + 4) % D;
            int bz = (sz - minBZ) % D;
            if (bx < 0) bx += D;
            if (by < 0) by += D;
            if (bz < 0) bz += D;
            return static_cast<u32>(bx) + static_cast<u32>(by) * D + static_cast<u32>(bz) * D * D;
        }

        static Vec3 OriginFor(int sx, int sy, int sz) {
            return Vec3(static_cast<float>(sx * 16), static_cast<float>(sy * 16),
                        static_cast<float>(sz * 16));
        }

        bool IsSolidAt(i64 x, i64 y, i64 z) const {
            if (y < -64)
                return true;
            if (y >= 320)
                return false;
            const int sx = static_cast<int>(x >> 4);
            const int sy = static_cast<int>(y >> 4);
            const int sz = static_cast<int>(z >> 4);
            const auto it = slots.find(Key(sx, sy, sz));
            if (it == slots.end() || !it->second.filled || it->second.brickIdx < 0)
                return true;
            const size_t vi = static_cast<size_t>((x & 15) + (y & 15) * 16 + (z & 15) * 256);
            const size_t w = static_cast<size_t>(it->second.brickIdx) * 128 + (vi >> 5u);
            if (w >= occMirror.size())
                return true;
            return ((occMirror[w] >> (vi & 31u)) & 1u) != 0u;
        }

        bool IsFluidAt(i64 x, i64 y, i64 z) const {
            if (y < -64 || y >= 320)
                return false;
            const int sx = static_cast<int>(x >> 4);
            const int sy = static_cast<int>(y >> 4);
            const int sz = static_cast<int>(z >> 4);
            const auto it = slots.find(Key(sx, sy, sz));
            if (it == slots.end() || !it->second.filled || it->second.brickIdx < 0)
                return false;
            const size_t vi = static_cast<size_t>((x & 15) + (y & 15) * 16 + (z & 15) * 256);
            const size_t w = static_cast<size_t>(it->second.brickIdx) * 128 + (vi >> 5u);
            if (w >= fluidMirror.size())
                return false;
            return ((fluidMirror[w] >> (vi & 31u)) & 1u) != 0u;
        }

        void RecomputeOpaque(CVoxelWorld &world, i32 brickIdx) {
            if (brickIdx < 0 || !pack)
                return;
            const size_t sbase = static_cast<size_t>(brickIdx) * 4096;
            if (sbase + 4096 > stateMirror.size())
                return;
            bool opaque = true;
            for (size_t vi = 0; vi < 4096; ++vi) {
                const u32 st = stateMirror[sbase + vi];
                const bool valid = static_cast<size_t>(st) < pack->states.size();
                const u32 flags = valid ? pack->states[static_cast<size_t>(st)].flags : 0u;
                const bool skip = !valid || ((flags & kBlockFlagSkip) != 0);
                if (skip || (flags & kBlockFlagOpaque) == 0u) {
                    opaque = false;
                    break;
                }
            }
            world.SetBrickOpaqueFull(static_cast<u32>(brickIdx), opaque);
        }

        i32 StateAt(i64 x, i64 y, i64 z) const {
            if (y < -64 || y >= 320)
                return -1;
            const int sx = static_cast<int>(x >> 4);
            const int sy = static_cast<int>(y >> 4);
            const int sz = static_cast<int>(z >> 4);
            const auto it = slots.find(Key(sx, sy, sz));
            if (it == slots.end() || !it->second.filled || it->second.brickIdx < 0)
                return -1;
            const size_t vi = static_cast<size_t>((x & 15) + (y & 15) * 16 + (z & 15) * 256);
            const size_t idx = static_cast<size_t>(it->second.brickIdx) * 4096 + vi;
            if (idx >= stateMirror.size())
                return -1;
            return static_cast<i32>(stateMirror[idx]);
        }

        bool CollisionBoxAt(i64 x, i64 y, i64 z, Vec3 &mn, Vec3 &mx) const {
            if (y >= 320)
                return false;
            if (y < -64) {
                mn = Vec3(static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(z));
                mx = mn + Vec3(1.f, 1.f, 1.f);
                return true;
            }
            if (!pack)
                return false;
            const int sx = static_cast<int>(x >> 4);
            const int sy = static_cast<int>(y >> 4);
            const int sz = static_cast<int>(z >> 4);
            const auto it = slots.find(Key(sx, sy, sz));
            if (it == slots.end() || !it->second.filled || it->second.brickIdx < 0) {
                mn = Vec3(static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(z));
                mx = mn + Vec3(1.f, 1.f, 1.f);
                return true;
            }
            const size_t vi = static_cast<size_t>((x & 15) + (y & 15) * 16 + (z & 15) * 256);
            const size_t idx = static_cast<size_t>(it->second.brickIdx) * 4096 + vi;
            if (idx >= stateMirror.size()) {
                mn = Vec3(static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(z));
                mx = mn + Vec3(1.f, 1.f, 1.f);
                return true;
            }
            const u16 st = stateMirror[idx];
            const size_t si = static_cast<size_t>(st);
            if (si >= pack->states.size() || si >= pack->fluid.size())
                return false;
            if (pack->fluid[si] != 0u)
                return false;
            const BlockStateLook_t &look = pack->states[si];
            if ((look.flags & kBlockFlagSkip) != 0u)
                return false;
            mn = Vec3(static_cast<float>(x) + look.collMin[0],
                      static_cast<float>(y) + look.collMin[1],
                      static_cast<float>(z) + look.collMin[2]);
            mx = Vec3(static_cast<float>(x) + look.collMax[0],
                      static_cast<float>(y) + look.collMax[1],
                      static_cast<float>(z) + look.collMax[2]);
            return mx.x > mn.x && mx.y > mn.y && mx.z > mn.z;
        }
    };

    CVoxelStreamWorld::CVoxelStreamWorld() : m_Impl(std::make_unique<Impl>()) {}
    CVoxelStreamWorld::~CVoxelStreamWorld() = default;

    Vec3 CVoxelStreamWorld::Init(CVoxelWorld &world, const BlockAssetPack_t &pack,
                                 const VoxelStreamDesc_t &desc) {
        m_Impl->desc = desc;
        m_Impl->pack = &pack;
        auto &I = *m_Impl;

        I.hasSave = false;
        if (!desc.worldDir.empty() && I.reader.Open(desc.worldDir)) {
            I.hasSave = true;
            Vec3 s{};
            if (I.reader.ReadSpawn(s))
                I.spawn = s + Vec3(0.f, 1.f, 0.f);
            else
                I.spawn = Vec3(8.f, 80.f, 8.f);
            std::printf("[StreamWorld] save %s spawn=(%.1f,%.1f,%.1f)\n", desc.worldDir.c_str(),
                        I.spawn.x, I.spawn.y, I.spawn.z);
        } else {

            I.spawn = Vec3(8.f, 80.f, 8.f);
            std::printf("[StreamWorld] ERROR: no region data in '%s' (pass --world-dir)\n",
                        desc.worldDir.c_str());
        }

        const int csx = static_cast<int>(std::floor(I.spawn.x / 16.f));
        const int csz = static_cast<int>(std::floor(I.spawn.z / 16.f));
        const int R = desc.radiusSections;
        world.SetWorldMin(Vec3(static_cast<float>((csx - R) * 16), -64.f,
                               static_cast<float>((csz - R) * 16)));
        I.minBX = csx - R;
        I.minBZ = csz - R;

        const int wantLive =
            (2 * (R + 1) + 1) * (kMaxSectionY - kMinSectionY + 1) * (2 * (R + 1) + 1);
        const int wantEvict2 =
            (2 * (R + 2) + 1) * (kMaxSectionY - kMinSectionY + 1) * (2 * (R + 2) + 1);
        if (wantEvict2 <= 8192)
            I.evictRadius = R + 2;
        else if (wantLive <= 8192)
            I.evictRadius = R + 1;
        else
            I.evictRadius = R;

        world.ReserveResident(8192);
        std::printf("[StreamWorld] streaming R=%d evict=%d save=%d spawn=(%.1f,%.1f,%.1f)\n", R,
                    I.evictRadius, I.hasSave ? 1 : 0, I.spawn.x, I.spawn.y, I.spawn.z);
        {
            constexpr size_t kReserveBricks = 8192;
            I.slots.reserve(kReserveBricks * 2);
            I.pending.reserve(128);
            const size_t window =
                static_cast<size_t>(2 * R + 1) * 24 * (2 * R + 1);
            I.unfilled.reserve(window);
            I.unfilledScratch.reserve(window);
            I.evictPending.reserve(1024);
            I.occMirror.assign(kReserveBricks * 128, 0u);
            I.fluidMirror.assign(kReserveBricks * 128, 0u);
            I.stateMirror.assign(kReserveBricks * 4096, 0u);
            I.brickContent.assign(kReserveBricks, 1u);
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
            std::printf("[StreamWorld] parse workers=%d fillBudget=%d maxPending=%zu\n",
                        numWorkers, I.desc.fillBudgetPerUpdate, I.maxPending);
            I.workerStates.reserve(static_cast<size_t>(numWorkers));
            I.workers.reserve(static_cast<size_t>(numWorkers));
            for (int w = 0; w < numWorkers; ++w) {
                I.workerStates.push_back(std::make_unique<Impl::WorkerState>());
                I.workers.emplace_back(&Impl::ParseLoop, &I,
                                       static_cast<size_t>(w));
            }
        }
        return I.spawn;
    }

    int CVoxelStreamWorld::UnfilledCount() const {
        return static_cast<int>(m_Impl->unfilled.size());
    }

    const VoxelStreamStats_t &CVoxelStreamWorld::GetLastStats() const {
        return m_Impl->lastStats;
    }

    int CVoxelStreamWorld::Update(CVoxelWorld &world, const Vec3 &cameraPos, u32 flightSlot) {
        auto &I = *m_Impl;
        using Clock = std::chrono::steady_clock;
        const auto tAll = Clock::now();
        auto tPhase = tAll;
        auto &ST = I.lastStats;
        ST = VoxelStreamStats_t{};
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
            for (const auto &kv : I.slots) {
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
                if (b >= 0) {
                    world.EvictBrick(static_cast<u32>(b));
                    if (static_cast<size_t>(b) < I.brickContent.size())
                        I.brickContent[static_cast<size_t>(b)] = 0u;
                }
                I.slots.erase(it);
                ++evicted;
            }
        }
        ST.evictMs = snapMs(tPhase);
        tPhase = Clock::now();

        {
            bool anyDone = false;
            for (const auto &s : I.workerStates) {
                if (!s->done.Empty()) {
                    anyDone = true;
                    break;
                }
            }
            if (I.unfilled.empty() && I.evictPending.empty() && !anyDone) {
                ST.totalMs = snapMs(tAll);
                ST.deferredBricks = static_cast<u32>(I.deferQueue.size());
                return 0;
            }
        }

        const int budget = I.desc.fillBudgetPerUpdate;

        thread_local std::vector<u16> batchMats;
        thread_local std::vector<u32> batchOcc;
        thread_local std::vector<u32> batchFluid;
        thread_local std::vector<u32> batchGpuOcc;
        thread_local std::vector<i64> filledKeys;
        thread_local std::vector<i64> freshKeys;
        thread_local std::vector<u32> batchIdx;
        thread_local std::vector<u8> batchOpaque;
        batchMats.resize(static_cast<size_t>(budget) * 4096);
        batchOcc.resize(static_cast<size_t>(budget) * 128);
        batchFluid.resize(static_cast<size_t>(budget) * 128);
        batchGpuOcc.resize(static_cast<size_t>(budget) * 128);
        batchIdx.clear();
        batchOpaque.clear();
        filledKeys.clear();
        freshKeys.clear();

        if (!I.unfilled.empty()) {
            size_t inFlight = I.pending.size();
            for (const auto &s : I.workerStates)
                inFlight += s->jobs.Size() + s->done.Size();
            if (inFlight < I.maxPending) {
                int enqueued = 0;
                const size_t numW = I.workerStates.size();
                for (const i64 key : I.unfilled) {
                    if (enqueued >= budget || inFlight >= I.maxPending)
                        break;
                    int sx, sy, sz;
                    Impl::DecodeKey(key, sx, sy, sz);
                    auto sit = I.slots.find(key);
                    u32 bidx = ~0u;
                    if (sit == I.slots.end()) {
                        const i32 nb = world.AllocateBrickSlot(
                            I.SlotFor(sx, sy, sz), Impl::OriginFor(sx, sy, sz));
                        if (nb < 0)
                            break;
                        sit = I.slots.emplace(key, Impl::SlotInfo{nb, false}).first;
                        bidx = static_cast<u32>(nb);
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
        ST.enqueueMs = snapMs(tPhase);
        tPhase = Clock::now();

        {
            int drained = 0;
            while (drained < budget) {
                bool progress = false;
                for (const auto &s : I.workerStates) {
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
                    batchIdx.push_back(d.brickIdx);
                    batchOpaque.push_back(d.opaque ? 1u : 0u);
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
        const auto commitOne = [&](u32 brickIdx, const u32 *occ, const u32 *fluid,
                                   const u16 *mats, u8 opaque, i64 key) {
            const size_t need = (static_cast<size_t>(brickIdx) + 1) * 128;
            if (I.occMirror.size() < need) {
                I.occMirror.resize(need, 0u);
                I.fluidMirror.resize(need, 0u);
            }
            std::memcpy(I.occMirror.data() + static_cast<size_t>(brickIdx) * 128, occ,
                        sizeof(u32) * 128);
            std::memcpy(I.fluidMirror.data() + static_cast<size_t>(brickIdx) * 128, fluid,
                        sizeof(u32) * 128);

            const size_t sneed = (static_cast<size_t>(brickIdx) + 1) * 4096;
            if (I.stateMirror.size() < sneed)
                I.stateMirror.resize(sneed, 0u);
            std::memcpy(I.stateMirror.data() + static_cast<size_t>(brickIdx) * 4096, mats,
                        sizeof(u16) * 4096);
            world.SetBrickOpaqueFull(brickIdx, opaque != 0u);

            world.MarkBrickAndNeighborsDirty(brickIdx);
            const auto sit = I.slots.find(key);
            if (sit != I.slots.end() &&
                sit->second.brickIdx == static_cast<i32>(brickIdx) && !sit->second.filled) {
                sit->second.filled = true;
                filledKeys.push_back(key);
            }
            I.pending.erase(key);
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
            const size_t n = std::min<size_t>(I.deferQueue.size(), free);
            if (n > 0) {
                stageMats.resize(n * 4096);
                stageOcc.resize(n * 128);
                stageIdx.resize(n);
                for (size_t k = 0; k < n; ++k) {
                    const auto &df = I.deferQueue[k];
                    std::memcpy(stageMats.data() + k * 4096, df.mats.data(),
                                sizeof(u16) * 4096);
                    for (int w = 0; w < 128; ++w)
                        stageOcc[k * 128 + w] = df.occ[w] | df.fluid[w];
                    stageIdx[k] = df.brickIdx;
                }
                if (world.StageBrickBatch(flightSlot, stageIdx.data(), stageMats.data(),
                                          stageOcc.data(), static_cast<u32>(n))) {
                    for (size_t k = 0; k < n; ++k) {
                        const auto &df = I.deferQueue[k];
                        commitOne(df.brickIdx, df.occ.data(), df.fluid.data(), df.mats.data(),
                                  df.opaque, df.key);
                    }
                    I.deferQueue.erase(I.deferQueue.begin(),
                                       I.deferQueue.begin() + static_cast<ptrdiff_t>(n));
                }
            }
        }

        if (!batchIdx.empty()) {
            for (size_t k = 0; k < batchIdx.size(); ++k) {
                const u32 *occ = batchOcc.data() + k * 128;
                const u32 *fld = batchFluid.data() + k * 128;
                u32 *gpu = batchGpuOcc.data() + k * 128;
                u32 any = 0u;
                for (int w = 0; w < 128; ++w) {
                    gpu[w] = occ[w] | fld[w];
                    any |= gpu[w];
                }
                const size_t bi = static_cast<size_t>(batchIdx[k]);
                if (I.brickContent.size() <= bi)
                    I.brickContent.resize(bi + 1, 1u);
                I.brickContent[bi] = (any != 0u) ? 1u : 0u;
            }
            const u32 free = world.GetStageFree(flightSlot);
            const u32 nStage =
                std::min<u32>(static_cast<u32>(batchIdx.size()), free);
            u32 nStaged = 0;
            if (nStage > 0 &&
                world.StageBrickBatch(flightSlot, batchIdx.data(), batchMats.data(),
                                      batchGpuOcc.data(), nStage)) {
                for (u32 k = 0; k < nStage; ++k) {
                    commitOne(batchIdx[k], batchOcc.data() + static_cast<size_t>(k) * 128,
                              batchFluid.data() + static_cast<size_t>(k) * 128,
                              batchMats.data() + static_cast<size_t>(k) * 4096,
                              batchOpaque[k], freshKeys[k]);
                }
                nStaged = nStage;
            }
            for (size_t k = nStaged; k < batchIdx.size(); ++k) {
                if (I.deferQueue.size() >= Impl::kMaxDeferred) {
                    ++I.totalDeferredDropped;
                    std::printf("[StreamWorld] deferral cap hit, dropping oldest (%zu queued)\n",
                                I.deferQueue.size());
                    I.deferQueue.erase(I.deferQueue.begin());
                }
                Impl::DeferredUpload_t df;
                df.key = freshKeys[k];
                df.brickIdx = batchIdx[k];
                df.opaque = batchOpaque[k];
                df.mats.assign(batchMats.begin() + static_cast<ptrdiff_t>(k * 4096),
                               batchMats.begin() + static_cast<ptrdiff_t>((k + 1) * 4096));
                df.occ.assign(batchOcc.begin() + static_cast<ptrdiff_t>(k * 128),
                              batchOcc.begin() + static_cast<ptrdiff_t>((k + 1) * 128));
                df.fluid.assign(batchFluid.begin() + static_cast<ptrdiff_t>(k * 128),
                                batchFluid.begin() + static_cast<ptrdiff_t>((k + 1) * 128));
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

        ST.commitMs = snapMs(tPhase);
        ST.totalMs = snapMs(tAll);
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

    void CVoxelStreamWorld::ApplyEdit(CVoxelWorld &world, const Vec3 &pos, float radius, u32 op,
                                      u32 state) {
        auto &I = *m_Impl;

        const int bsx = static_cast<int>(std::floor(pos.x / 16.f));
        const int bsy = static_cast<int>(std::floor(pos.y / 16.f));
        const int bsz = static_cast<int>(std::floor(pos.z / 16.f));
        const auto it = I.slots.find(Impl::Key(bsx, bsy, bsz));
        if (it == I.slots.end() || !it->second.filled || it->second.brickIdx < 0)
            return;
        const size_t base = static_cast<size_t>(it->second.brickIdx) * 128;
        if (base + 128 > I.occMirror.size() || base + 128 > I.fluidMirror.size())
            return;
        const size_t sbase = static_cast<size_t>(it->second.brickIdx) * 4096;
        const bool hasState = sbase + 4096 <= I.stateMirror.size();
        const auto applyVoxel = [&](int vx, int vy, int vz) {

            if ((vx >> 4) != bsx || (vy >> 4) != bsy || (vz >> 4) != bsz)
                return;
            const size_t vi =
                static_cast<size_t>((vx & 15) + (vy & 15) * 16 + (vz & 15) * 256);
            const size_t w = base + (vi >> 5u);
            const u32 bit = 1u << (vi & 31u);
            if (op == 0u) {
                I.occMirror[w] &= ~bit;
                I.fluidMirror[w] &= ~bit;
                if (hasState)
                    I.stateMirror[sbase + vi] = 0u;
            } else {
                I.occMirror[w] |= bit;
                I.fluidMirror[w] &= ~bit;
                if (hasState)
                    I.stateMirror[sbase + vi] = static_cast<u16>(state & 0xFFFFu);
            }
        };
        if (radius < 0.5f) {
            applyVoxel(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)),
                       static_cast<int>(std::floor(pos.z)));
            I.RecomputeOpaque(world, it->second.brickIdx);
        } else {
            const float reach = radius + 0.5f;
            const int x0 = static_cast<int>(std::floor(pos.x - reach));
            const int x1 = static_cast<int>(std::floor(pos.x + reach));
            const int y0 = static_cast<int>(std::floor(pos.y - reach));
            const int y1 = static_cast<int>(std::floor(pos.y + reach));
            const int z0 = static_cast<int>(std::floor(pos.z - reach));
            const int z1 = static_cast<int>(std::floor(pos.z + reach));
            for (int vz = z0; vz <= z1; ++vz) {
                for (int vy = y0; vy <= y1; ++vy) {
                    for (int vx = x0; vx <= x1; ++vx) {

                        const float dx = static_cast<float>(vx) + 0.5f - pos.x;
                        const float dy = static_cast<float>(vy) + 0.5f - pos.y;
                        const float dz = static_cast<float>(vz) + 0.5f - pos.z;
                        if (std::sqrt(dx * dx + dy * dy + dz * dz) > reach)
                            continue;
                        applyVoxel(vx, vy, vz);
                    }
                }
            }
            I.RecomputeOpaque(world, it->second.brickIdx);
        }
        {
            const size_t bi = static_cast<size_t>(it->second.brickIdx);
            u32 any = 0u;
            for (size_t w = 0; w < 128; ++w)
                any |= (I.occMirror[base + w] | I.fluidMirror[base + w]);
            if (I.brickContent.size() <= bi)
                I.brickContent.resize(bi + 1, 1u);
            I.brickContent[bi] = (any != 0u) ? 1u : 0u;
        }
    }

    bool CVoxelStreamWorld::IsSolidAt(i64 x, i64 y, i64 z) const { return m_Impl->IsSolidAt(x, y, z); }

    bool CVoxelStreamWorld::IsFluidAt(i64 x, i64 y, i64 z) const { return m_Impl->IsFluidAt(x, y, z); }

    i32 CVoxelStreamWorld::GetStateAt(i64 x, i64 y, i64 z) const { return m_Impl->StateAt(x, y, z); }

    bool CVoxelStreamWorld::GetCollisionBoxAt(i64 x, i64 y, i64 z, Vec3 &mn,
                                               Vec3 &mx) const {
        return m_Impl->CollisionBoxAt(x, y, z, mn, mx);
    }

    bool CVoxelStreamWorld::BrickHasContent(u32 brickIdx) const {
        auto &I = *m_Impl;
        if (static_cast<size_t>(brickIdx) >= I.brickContent.size())
            return true;
        return I.brickContent[static_cast<size_t>(brickIdx)] != 0u;
    }

    i32 CVoxelStreamWorld::PlaceState() const {
        return static_cast<i32>(m_Impl->pack->placeState);
    }
}
