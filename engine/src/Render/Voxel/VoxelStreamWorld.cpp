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
#include <shared_mutex>
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

        // Flat v1 light seed (worker-local, async, no flood-fill): sky column
        // from true top through save opacity + block emission. Horizontal
        // falloff converges via the main-thread border pull (1 step per load)
        // with async lag. v2 restores full multi-step smooth propagation.
        inline void SeedFlatLight(const BlockAssetPack_t &pack, CAnvilWorldReader &reader,
                                  int sx, int sy, int sz, const std::vector<u16> &mats,
                                  std::vector<u8> &lightOut) {
            lightOut.assign(4096, 0);
            const size_t emisSize = pack.emission.size();
            const size_t skyOpSize = pack.skyOpacity.size();
            const auto skyOpOf = [&](u32 st) -> u8 {
                return (static_cast<size_t>(st) < skyOpSize)
                           ? pack.skyOpacity[static_cast<size_t>(st)]
                           : 15u;
            };
            const auto emisOf = [&](u32 st) -> u8 {
                return (static_cast<size_t>(st) < emisSize)
                           ? pack.emission[static_cast<size_t>(st)]
                           : 0u;
            };
            // Sky entering own top per (lx,lz): walk sections above via save
            // (chunk-column cached, absent = air). Skipped for top section.
            u8 topSky[16][16];
            for (int lz = 0; lz < 16; ++lz)
                for (int lx = 0; lx < 16; ++lx)
                    topSky[lx][lz] = 15u;
            if (sy < kMaxSectionY) {
                AnvilSectionStates sec{};
                std::vector<u32> palTmp;
                for (int asy = kMaxSectionY; asy > sy; --asy) {
                    if (!reader.ReadSectionStates(sx, asy, sz, sec) || !sec.present)
                        continue; // air above
                    palTmp.clear();
                    palTmp.reserve(sec.palette.size());
                    for (const FullState &fs : sec.palette) {
                        const auto it =
                            pack.stateByKey.find(MakeStateKey(fs.name, fs.props));
                        palTmp.push_back(it != pack.stateByKey.end()
                                             ? it->second
                                             : pack.fallbackState);
                    }
                    for (int lz = 0; lz < 16; ++lz) {
                        for (int lx = 0; lx < 16; ++lx) {
                            if (topSky[lx][lz] == 0u)
                                continue;
                            for (int ly = 15; ly >= 0; --ly) {
                                const u32 st =
                                    palTmp[sec.pal[lx + ly * 16 + lz * 256]];
                                if (skyOpOf(st) >= 15u) {
                                    topSky[lx][lz] = 0u;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            for (int lz = 0; lz < 16; ++lz) {
                for (int lx = 0; lx < 16; ++lx) {
                    u8 carry = topSky[lx][lz];
                    for (int ly = 15; ly >= 0; --ly) {
                        const size_t vi = static_cast<size_t>(lx + ly * 16 + lz * 256);
                        const u32 st = mats[vi];
                        const u8 emis = emisOf(st);
                        const u8 sop = skyOpOf(st);
                        const int gy = sy * 16 + ly;
                        u8 sky = 0u;
                        if (gy >= 320)
                            sky = 15u;
                        else if (gy < -64)
                            sky = 0u;
                        else if (sop >= 15u)
                            sky = 0u;
                        else
                            sky = carry;
                        lightOut[vi] = static_cast<u8>((sky << 4u) | (emis & 0x0Fu));
                        if (gy < -64 || gy >= 320) {
                            /* carry unchanged outside world */
                        } else if (sop >= 15u) {
                            carry = 0u;
                        } else if (sop != 0u) {
                            carry = carry > 0u ? static_cast<u8>(carry - 1u) : 0u;
                        }
                    }
                }
            }
        }
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
        // O(1) slot -> filled brickIdx (or -1). Mirrors CVoxelWorld page table for
        // the lighting hot path (avoids unordered_map hashing per neighbour).
        std::vector<i32> slotMirror;

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
            // Flat packed light (sky<<4|block), seeded async in the worker
            // (sky column from true top via save + emission). Attached to the
            // subchunk; uploaded with the brick (no light bricks, no flood).
            std::vector<u8> light;
            bool opaque{false};
            bool hasEmission{false};
            u64 lightVisits{0}; // worker BFS visits (accumulated into stats)
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
            u8 hasEmis{0};
            std::vector<u16> mats;
            std::vector<u32> occ;
            std::vector<u32> fluid;
            std::vector<u8> light;
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
                bool hasEmis = false;
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
                    // Keep original state id (even for skip/air-like blocks such as
                    // torches) so CPU lighting can look up emission. GPU face
                    // culling still skips via the skip flag, so rendering is
                    // unchanged. Occupancy stays air-like for skip.
                    out.mats[static_cast<size_t>(vi)] = valid ? static_cast<u16>(st) : 0;
                    if (valid && static_cast<size_t>(st) < pack->emission.size() &&
                        pack->emission[static_cast<size_t>(st)] != 0u)
                        hasEmis = true;
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
                out.hasEmission = hasEmis;
                // Async full spread in the worker: column seed + halo inflow +
                // bounded in-brick BFS. Main thread never flood-fills.
                out.lightVisits = SeedSpreadLight(job.sx, job.sy, job.sz, out.mats,
                                                 out.light, localReader);
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
        // Packed vanilla light: high nibble sky (0-15), low nibble block (0-15).
        std::vector<u8> lightMirror;
        std::vector<u8> lightDirtyFlag;
        std::vector<u32> lightDirtyList;
        // Guards slots/slotMirror/occMirror/fluidMirror/stateMirror/
        // lightMirror/brickContent. Main thread (sole writer) takes unique
        // locks at mutation sites; parse workers take brief shared locks to
        // snapshot neighbour light for async BFS. Main-thread reads
        // (physics/picking, same thread as writer) need no lock.
        mutable std::shared_mutex m_MirrorMutex;
        // ---- Vanilla light perf instrumentation (Step A: bounded flood-fill) ----
        // Accumulated per Update() and flushed into VoxelStreamStats_t.
        // (ApplyEdit also accumulates; attributed to the next Update.)
        double lightMsAccum{0.0};
        u32 lightBrickCount{0};
        u64 lightVisitCount{0};
        size_t lightQueuePeak{0};
        u64 updateCount{0};
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
            // D=64 is a power of two: bitmask instead of 3 idivs per call.
            // (u32) truncation + &63 == ((v%64)+64)%64 for all ints, so the
            // mapping is identical to the old modulo version, just ~30x
            // cheaper. Matters for the per-neighbour lighting lookups.
            constexpr u32 M = 64u - 1u;
            const u32 bx = static_cast<u32>(sx - minBX) & M;
            const u32 by = static_cast<u32>(sy + 4) & M;
            const u32 bz = static_cast<u32>(sz - minBZ) & M;
            return bx + by * 64u + bz * 64u * 64u;
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

        // Flat v1 neighbour fetch: packed light at a global voxel via the
        // resident slot mirror. Returns false when unloaded (no contribution).
        bool PackedLightAt(i64 x, i64 y, i64 z, u8 &skyOut, u8 &blkOut) const {
            if (y >= 320) {
                skyOut = 15u;
                blkOut = 0u;
                return true;
            }
            if (y < -64)
                return false;
            const int sx = static_cast<int>(x >> 4);
            const int sy = static_cast<int>(y >> 4);
            const int sz = static_cast<int>(z >> 4);
            const u32 slot = SlotFor(sx, sy, sz);
            if (slot >= slotMirror.size())
                return false;
            const i32 b = slotMirror[slot];
            if (b < 0)
                return false;
            const u32 vi = static_cast<u32>((x & 15) + (y & 15) * 16 + (z & 15) * 256);
            const size_t idx = static_cast<size_t>(b) * 4096 + vi;
            if (idx >= lightMirror.size())
                return false;
            const u8 p = lightMirror[idx];
            skyOut = static_cast<u8>((p >> 4u) & 0xFu);
            blkOut = static_cast<u8>(p & 0xFu);
            return true;
        }

        // Flat v1 border pull (sync, bounded, no BFS): raise-only 1-step
        // falloff from already-loaded face neighbours so seams converge with
        // async lag. Sky columns are seeded from true top in the worker, so
        // no commit ordering is required. v2 restores full propagation.
        void BorderPullLight(u32 brickIdx, int bsx, int bsy, int bsz) {
            if (!pack)
                return;
            const size_t base = static_cast<size_t>(brickIdx) * 4096;
            if (base + 4096 > lightMirror.size() || base + 4096 > stateMirror.size())
                return;
            static const int kNb[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                          {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            const size_t emisSize = pack->emission.size();
            const size_t skyOpSize = pack->skyOpacity.size();
            for (int ly = 0; ly < 16; ++ly) {
                for (int lz = 0; lz < 16; ++lz) {
                    for (int lx = 0; lx < 16; ++lx) {
                        if (lx != 0 && lx != 15 && ly != 0 && ly != 15 && lz != 0 &&
                            lz != 15)
                            continue;
                        const size_t vi = static_cast<size_t>(lx + ly * 16 + lz * 256);
                        const u32 st = stateMirror[base + vi];
                        const u8 sop = (static_cast<size_t>(st) < skyOpSize)
                                           ? pack->skyOpacity[static_cast<size_t>(st)]
                                           : 15u;
                        if (sop >= 15u)
                            continue; // opaque: worker seed (emission) stands
                        const int gx = bsx * 16 + lx;
                        const int gy = bsy * 16 + ly;
                        const int gz = bsz * 16 + lz;
                        u8 bestSky = static_cast<u8>(
                            (lightMirror[base + vi] >> 4u) & 0xFu);
                        u8 bestBlk = static_cast<u8>(lightMirror[base + vi] & 0xFu);
                        for (const auto &o : kNb) {
                            u8 ns = 0u, nb = 0u;
                            if (!PackedLightAt(gx + o[0], gy + o[1], gz + o[2], ns, nb))
                                continue;
                            u8 skyCand;
                            if (ns == 15 && o[1] == -1 && sop == 0u)
                                skyCand = 15u; // open-sky column runs down
                            else
                                skyCand = ns > 0u ? static_cast<u8>(ns - 1u) : 0u;
                            const u8 blkCand = nb > 0u ? static_cast<u8>(nb - 1u) : 0u;
                            if (skyCand > bestSky)
                                bestSky = skyCand;
                            if (blkCand > bestBlk)
                                bestBlk = blkCand;
                        }
                        const u8 emis = (static_cast<size_t>(st) < emisSize)
                                            ? pack->emission[static_cast<size_t>(st)]
                                            : 0u;
                        if (emis > bestBlk)
                            bestBlk = emis;
                        lightMirror[base + vi] =
                            static_cast<u8>((bestSky << 4u) | (bestBlk & 0xFu));
                    }
                }
            }
        }

        static constexpr int kHaloDir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                              {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

        struct HaloSnap_t {
            bool loaded{false};
            u8 light[4096]{};
        };

        // Snapshot the 6 face-neighbour light arrays under a brief shared
        // lock (up to 6x4KB). Stale snapshots self-heal via BorderPull and
        // future loads (async lag); callers must tolerate lag.
        void SnapshotHalo(int bsx, int bsy, int bsz, HaloSnap_t out[6]) const {
            std::shared_lock lk(m_MirrorMutex);
            for (int a = 0; a < 6; ++a) {
                HaloSnap_t &h = out[a];
                h.loaded = false;
                const int nsx = bsx + kHaloDir[a][0];
                const int nsy = bsy + kHaloDir[a][1];
                const int nsz = bsz + kHaloDir[a][2];
                if (nsy < kMinSectionY || nsy > kMaxSectionY)
                    continue;
                const auto it = slots.find(Key(nsx, nsy, nsz));
                if (it == slots.end() || !it->second.filled || it->second.brickIdx < 0)
                    continue;
                const size_t base = static_cast<size_t>(it->second.brickIdx) * 4096;
                if (base + 4096 > lightMirror.size())
                    continue;
                std::memcpy(h.light, lightMirror.data() + base, 4096);
                h.loaded = true;
            }
        }

        // Halo level for a global voxel known to be outside the own brick.
        // Edge rules mirror the sampler: above world = full sky, below = dark,
        // unloaded = dark (converges on load).
        static bool HaloLevel(const HaloSnap_t halo[6], int bsx, int bsy, int bsz,
                              int gx, int gy, int gz, u8 &skyOut, u8 &blkOut) {
            if (gy >= 320) {
                skyOut = 15u;
                blkOut = 0u;
                return true;
            }
            if (gy < -64)
                return false;
            const int nsx = gx >> 4, nsy = gy >> 4, nsz = gz >> 4;
            for (int a = 0; a < 6; ++a) {
                if (nsx == bsx + kHaloDir[a][0] && nsy == bsy + kHaloDir[a][1] &&
                    nsz == bsz + kHaloDir[a][2]) {
                    if (!halo[a].loaded)
                        return false;
                    const u32 vi =
                        static_cast<u32>((gx & 15) + (gy & 15) * 16 + (gz & 15) * 256);
                    const u8 p = halo[a].light[vi];
                    skyOut = static_cast<u8>((p >> 4u) & 0xFu);
                    blkOut = static_cast<u8>(p & 0xFu);
                    return true;
                }
            }
            return false;
        }

        // Async full spread (worker thread): column+emission seed, halo
        // inflow, then bounded multi-step BFS confined to the own 16^3.
        // Gives full 15-falloff inside the brick; cross-brick glow arrives via
        // halo snapshots when neighbours are loaded (else truncated with lag).
        // Never touches main-thread mirrors except the halo snapshot.
        u64 SeedSpreadLight(int sx, int sy, int sz, const std::vector<u16> &mats,
                            std::vector<u8> &lightOut,
                            CAnvilWorldReader &reader) const {
            SeedFlatLight(*pack, reader, sx, sy, sz, mats, lightOut);
            if (!pack)
                return 0;
            bool anyLight = false;
            for (size_t vi = 0; vi < 4096; ++vi) {
                if (lightOut[vi] != 0u) {
                    anyLight = true;
                    break;
                }
            }
            if (!anyLight)
                return 0; // fully dark: no BFS needed
            HaloSnap_t halo[6];
            SnapshotHalo(sx, sy, sz, halo);
            const size_t skyOpSize = pack->skyOpacity.size();
            const size_t stateSize = pack->states.size();
            const auto skyOpOf = [&](u32 st) -> u8 {
                return (static_cast<size_t>(st) < skyOpSize)
                           ? pack->skyOpacity[static_cast<size_t>(st)]
                           : 15u;
            };
            // Border inflow from halo + edge rules (raise-only).
            for (int ly = 0; ly < 16; ++ly) {
                for (int lz = 0; lz < 16; ++lz) {
                    for (int lx = 0; lx < 16; ++lx) {
                        if (lx != 0 && lx != 15 && ly != 0 && ly != 15 && lz != 0 &&
                            lz != 15)
                            continue;
                        const size_t vi = static_cast<size_t>(lx + ly * 16 + lz * 256);
                        const u32 st = mats[vi];
                        if (skyOpOf(st) >= 15u)
                            continue;
                        const int gx = sx * 16 + lx;
                        const int gy = sy * 16 + ly;
                        const int gz = sz * 16 + lz;
                        const u8 sop = skyOpOf(st);
                        u8 bestSky =
                            static_cast<u8>((lightOut[vi] >> 4u) & 0xFu);
                        u8 bestBlk = static_cast<u8>(lightOut[vi] & 0xFu);
                        for (const auto &o : kHaloDir) {
                            const int nx = gx + o[0];
                            const int ny = gy + o[1];
                            const int nz = gz + o[2];
                            u8 ns = 0u, nb = 0u;
                            if (nx >= sx * 16 && nx < sx * 16 + 16 && ny >= sy * 16 &&
                                ny < sy * 16 + 16 && nz >= sz * 16 && nz < sz * 16 + 16) {
                                const size_t nvi = static_cast<size_t>(
                                    (nx & 15) + (ny & 15) * 16 + (nz & 15) * 256);
                                ns = static_cast<u8>((lightOut[nvi] >> 4u) & 0xFu);
                                nb = static_cast<u8>(lightOut[nvi] & 0xFu);
                            } else if (!HaloLevel(halo, sx, sy, sz, nx, ny, nz, ns,
                                                  nb)) {
                                continue;
                            }
                            u8 skyCand;
                            if (ns == 15 && o[1] == -1 && sop == 0u)
                                skyCand = 15u;
                            else
                                skyCand = ns > 0u ? static_cast<u8>(ns - 1u) : 0u;
                            const u8 blkCand = nb > 0u ? static_cast<u8>(nb - 1u) : 0u;
                            if (skyCand > bestSky)
                                bestSky = skyCand;
                            if (blkCand > bestBlk)
                                bestBlk = blkCand;
                        }
                        lightOut[vi] =
                            static_cast<u8>((bestSky << 4u) | (bestBlk & 0xFu));
                    }
                }
            }
            // Bounded BFS over the own brick (raise-only, writes own only).
            thread_local std::vector<u16> wSkyQ, wBlkQ;
            wSkyQ.clear();
            wBlkQ.clear();
            // Seed every lit voxel: raise-only BFS then converges in-brick.
            // (Border-only seeding would miss interior skylight shafts next
            // to dark pockets.) Queue/visit caps bound pathological cases.
            for (size_t vi = 0; vi < 4096; ++vi) {
                const u8 p = lightOut[vi];
                if (p == 0u)
                    continue;
                if (((p >> 4u) & 0xFu) != 0u)
                    wSkyQ.push_back(static_cast<u16>(vi));
                if ((p & 0xFu) != 0u)
                    wBlkQ.push_back(static_cast<u16>(vi));
            }
            u64 visits = 0;
            constexpr u64 kMaxVisits = 49152;
            constexpr size_t kMaxQ = 16384;
            size_t head = 0;
            while (head < wSkyQ.size() && visits < kMaxVisits) {
                const u16 vi = wSkyQ[head++];
                ++visits;
                const u8 cur = static_cast<u8>((lightOut[vi] >> 4u) & 0xFu);
                if (cur == 0u)
                    continue;
                const int lx = vi & 15;
                const int ly = (vi >> 4) & 15;
                const int lz = (vi >> 8) & 15;
                for (const auto &o : kHaloDir) {
                    const int nlx = lx + o[0];
                    const int nly = ly + o[1];
                    const int nlz = lz + o[2];
                    if (nlx < 0 || nlx > 15 || nly < 0 || nly > 15 || nlz < 0 ||
                        nlz > 15)
                        continue; // writes confined to own brick
                    const size_t nvi =
                        static_cast<size_t>(nlx + nly * 16 + nlz * 256);
                    const u32 nst = mats[nvi];
                    const u8 nsop = skyOpOf(nst);
                    if (nsop >= 15u)
                        continue;
                    u8 want;
                    if (cur == 15 && o[1] == -1 && nsop == 0u)
                        want = 15u;
                    else
                        want = static_cast<u8>(cur - 1u);
                    const u8 have = static_cast<u8>((lightOut[nvi] >> 4u) & 0xFu);
                    if (want > have) {
                        lightOut[nvi] = static_cast<u8>(
                            (lightOut[nvi] & 0x0Fu) | (want << 4u));
                        if (wSkyQ.size() < kMaxQ)
                            wSkyQ.push_back(static_cast<u16>(nvi));
                    }
                }
            }
            head = 0;
            while (head < wBlkQ.size() && visits < kMaxVisits) {
                const u16 vi = wBlkQ[head++];
                ++visits;
                const u8 cur = static_cast<u8>(lightOut[vi] & 0xFu);
                if (cur <= 1u)
                    continue; // cur-1 would be 0: no spread
                const int lx = vi & 15;
                const int ly = (vi >> 4) & 15;
                const int lz = (vi >> 8) & 15;
                for (const auto &o : kHaloDir) {
                    const int nlx = lx + o[0];
                    const int nly = ly + o[1];
                    const int nlz = lz + o[2];
                    if (nlx < 0 || nlx > 15 || nly < 0 || nly > 15 || nlz < 0 ||
                        nlz > 15)
                        continue;
                    const size_t nvi =
                        static_cast<size_t>(nlx + nly * 16 + nlz * 256);
                    const u32 nst = mats[nvi];
                    const bool nOpaque =
                        (static_cast<size_t>(nst) < stateSize) &&
                        ((pack->states[nst].flags & kBlockFlagOpaque) != 0u);
                    if (nOpaque)
                        continue;
                    const u8 want = static_cast<u8>(cur - 1u);
                    const u8 have = static_cast<u8>(lightOut[nvi] & 0xFu);
                    if (want > have) {
                        lightOut[nvi] = static_cast<u8>(
                            (lightOut[nvi] & 0xF0u) | want);
                        if (wBlkQ.size() < kMaxQ)
                            wBlkQ.push_back(static_cast<u16>(nvi));
                    }
                }
            }
            return visits;
        }

        // Emissive-brick glow push (main thread, mirror lock held): raise the
        // 1-halo of loaded face neighbours and queue them for restage/rebake
        // so torch glow crosses chunk borders with async lag.
        void GlowPushToNeighbours(u32 brickIdx, int bsx, int bsy, int bsz) {
            if (!pack)
                return;
            const size_t base = static_cast<size_t>(brickIdx) * 4096;
            if (base + 4096 > lightMirror.size() || base + 4096 > stateMirror.size())
                return;
            const size_t skyOpSize = pack->skyOpacity.size();
            for (int a = 0; a < 6; ++a) {
                const int nsx = bsx + kHaloDir[a][0];
                const int nsy = bsy + kHaloDir[a][1];
                const int nsz = bsz + kHaloDir[a][2];
                if (nsy < kMinSectionY || nsy > kMaxSectionY)
                    continue;
                const auto it = slots.find(Key(nsx, nsy, nsz));
                if (it == slots.end() || !it->second.filled || it->second.brickIdx < 0)
                    continue;
                const u32 nb = static_cast<u32>(it->second.brickIdx);
                const size_t nbase = static_cast<size_t>(nb) * 4096;
                if (nbase + 4096 > lightMirror.size() ||
                    nbase + 4096 > stateMirror.size())
                    continue;
                bool raised = false;
                for (int t0 = 0; t0 < 16; ++t0) {
                    for (int t1 = 0; t1 < 16; ++t1) {
                        int olx, oly, olz, nlx, nly, nlz;
                        if (kHaloDir[a][0] != 0) {
                            olx = (kHaloDir[a][0] > 0) ? 15 : 0;
                            nlx = (kHaloDir[a][0] > 0) ? 0 : 15;
                            oly = nly = t0;
                            olz = nlz = t1;
                        } else if (kHaloDir[a][1] != 0) {
                            oly = (kHaloDir[a][1] > 0) ? 15 : 0;
                            nly = (kHaloDir[a][1] > 0) ? 0 : 15;
                            olx = nlx = t0;
                            olz = nlz = t1;
                        } else {
                            olz = (kHaloDir[a][2] > 0) ? 15 : 0;
                            nlz = (kHaloDir[a][2] > 0) ? 0 : 15;
                            olx = nlx = t0;
                            oly = nly = t1;
                        }
                        const size_t ovi =
                            static_cast<size_t>(olx + oly * 16 + olz * 256);
                        const size_t nvi =
                            static_cast<size_t>(nlx + nly * 16 + nlz * 256);
                        const u8 op = lightMirror[base + ovi];
                        const u8 osky = static_cast<u8>((op >> 4u) & 0xFu);
                        const u8 oblk = static_cast<u8>(op & 0xFu);
                        if (osky == 0u && oblk == 0u)
                            continue;
                        const u32 nst = stateMirror[nbase + nvi];
                        const u8 nsop = (static_cast<size_t>(nst) < skyOpSize)
                                            ? pack->skyOpacity[nst]
                                            : 15u;
                        if (nsop >= 15u)
                            continue;
                        u8 wantSky;
                        if (osky == 15 && kHaloDir[a][1] == -1 && nsop == 0u)
                            wantSky = 15u;
                        else
                            wantSky = osky > 0u ? static_cast<u8>(osky - 1u) : 0u;
                        const u8 wantBlk = oblk > 0u ? static_cast<u8>(oblk - 1u) : 0u;
                        const u8 cur = lightMirror[nbase + nvi];
                        const u8 curSky = static_cast<u8>((cur >> 4u) & 0xFu);
                        const u8 curBlk = static_cast<u8>(cur & 0xFu);
                        const u8 newSky = wantSky > curSky ? wantSky : curSky;
                        const u8 newBlk = wantBlk > curBlk ? wantBlk : curBlk;
                        if (newSky != curSky || newBlk != curBlk) {
                            lightMirror[nbase + nvi] =
                                static_cast<u8>((newSky << 4u) | (newBlk & 0xFu));
                            raised = true;
                        }
                    }
                }
                if (raised) {
                    const i64 nkey = Key(nsx, nsy, nsz);
                    bool dup = false;
                    for (const auto &q : editRestage) {
                        if (q.key == nkey) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup)
                        editRestage.push_back({nkey, nb});
                }
            }
        }

        struct EditRestage_t {
            i64 key{-1};
            u32 brick{~0u};
        };
        // Bricks whose CPU mirrors changed via ApplyEdit and need a full
        // brickEx re-stage (voxels + flat light). Drained in Update with
        // 1-frame async lag; pruned on evict via the slots map.
        std::vector<EditRestage_t> editRestage;

        i32 StateAt(i64 x, i64 y, i64 z) const {
            if (y < -64 || y >= 320)
                return -1;
            const int sx = static_cast<int>(x >> 4);
            const int sy = static_cast<int>(y >> 4);
            const int sz = static_cast<int>(z >> 4);
            const u32 slot = SlotFor(sx, sy, sz);
            if (slot >= slotMirror.size())
                return -1;
            const i32 b = slotMirror[slot];
            if (b < 0)
                return -1;
            const size_t vi = static_cast<size_t>((x & 15) + (y & 15) * 16 + (z & 15) * 256);
            const size_t idx = static_cast<size_t>(b) * 4096 + vi;
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

        // NOTE (v2): the old main-thread flood-fill lighting (~700 lines:
        // voxel helpers, dirty flags, BFS queues, per-brick/box/edit
        // recomputes) was deleted. Lighting is seeded async in workers
        // (SeedFlatLight/SeedSpreadLight) and converged via BorderPullLight +
        // GlowPushToNeighbours with lag. Edits re-stage via editRestage.
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
            I.lightMirror.assign(kReserveBricks * 4096, 0u);
            I.lightDirtyFlag.assign(kReserveBricks, 0u);
            I.lightDirtyList.reserve(256);
            {
                const u32 vd = world.GetVirtualDim() != 0u ? world.GetVirtualDim() : 64u;
                I.slotMirror.assign(static_cast<size_t>(vd) * vd * vd, -1);
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
                    // No light-brick clear: the slot is dropped and the next
                    // commit fully overwrites mirrors + brickEx upload.
                }
                {
                    const u32 slot = I.SlotFor(sx, sy, sz);
                    std::unique_lock lk(I.m_MirrorMutex);
                    if (slot < I.slotMirror.size())
                        I.slotMirror[slot] = -1;
                    I.slots.erase(it);
                }
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
                        const u32 slot = I.SlotFor(sx, sy, sz);
                        const i32 nb = world.AllocateBrickSlot(
                            slot, Impl::OriginFor(sx, sy, sz));
                        if (nb < 0)
                            break;
                        bidx = static_cast<u32>(nb);
                        {
                            // Mirror lock: workers snapshot slots concurrently.
                            // (AllocateBrickSlot above stays outside: GPU bind.)
                            std::unique_lock lk(I.m_MirrorMutex);
                            sit = I.slots.emplace(key, Impl::SlotInfo{nb, false}).first;
                            if (slot < I.slotMirror.size())
                                I.slotMirror[slot] = -1; // reserved, not loaded yet
                        }
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
            // Mirror lock: workers snapshot neighbour light concurrently.
            std::unique_lock lk(I.m_MirrorMutex);
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

            // Flat light: worker-seeded array rides the brick (no flood-fill
            // on the main thread). Border pull converges seams with lag.
            {
                const auto t0 = std::chrono::steady_clock::now();
                const size_t lneed = (static_cast<size_t>(brickIdx) + 1) * 4096;
                if (I.lightMirror.size() < lneed)
                    I.lightMirror.resize(lneed, 0u);
                std::memcpy(I.lightMirror.data() + static_cast<size_t>(brickIdx) * 4096,
                            light, 4096);
                int dsx = 0, dsy = 0, dsz = 0;
                Impl::DecodeKey(key, dsx, dsy, dsz);
                const u32 slot = I.SlotFor(dsx, dsy, dsz);
                if (slot < I.slotMirror.size())
                    I.slotMirror[slot] = static_cast<i32>(brickIdx);
                if (opaque == 0u || hasEmis != 0u)
                    I.BorderPullLight(brickIdx, dsx, dsy, dsz);
                if (hasEmis != 0u)
                    I.GlowPushToNeighbours(brickIdx, dsx, dsy, dsz);
                I.lightMsAccum += std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0)
                                      .count();
                ++I.lightBrickCount;
            }
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

        // Edited bricks: re-stage full brickEx (voxels + flat light) with
        // 1-frame async lag. Wins staging over new bricks (user-visible).
        if (!I.editRestage.empty()) {
            // Mirror lock: workers snapshot mirrors concurrently.
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
            const size_t n = std::min<size_t>(I.editRestage.size(), free);
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

    void CVoxelStreamWorld::ApplyEdit(CVoxelWorld &world, const Vec3 &pos, float radius, u32 op,
                                      u32 state) {
        auto &I = *m_Impl;
        // Mirror lock: workers snapshot mirrors concurrently.
        std::unique_lock lk(I.m_MirrorMutex);

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
        const size_t libase = static_cast<size_t>(it->second.brickIdx) * 4096;
        const bool hasLight = libase + 4096 <= I.lightMirror.size();
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
            // Flat v1 edit light (sync, raise-only 1-step neighbour pull).
            if (hasLight) {
                const u32 stNow = hasState ? I.stateMirror[sbase + vi] : 0u;
                u8 emis = 0u, sop = 15u;
                if (I.pack) {
                    if (static_cast<size_t>(stNow) < I.pack->emission.size())
                        emis = I.pack->emission[static_cast<size_t>(stNow)];
                    if (static_cast<size_t>(stNow) < I.pack->skyOpacity.size())
                        sop = I.pack->skyOpacity[static_cast<size_t>(stNow)];
                }
                u8 sky = 0u, blk = emis;
                if (vy >= 320) {
                    sky = 15u;
                } else if (vy >= -64 && sop < 15u) {
                    static const int kNb[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                                  {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    for (const auto &o : kNb) {
                        u8 ns = 0u, nb = 0u;
                        if (!I.PackedLightAt(vx + o[0], vy + o[1], vz + o[2], ns, nb))
                            continue;
                        u8 skyCand;
                        if (ns == 15 && o[1] == -1 && sop == 0u)
                            skyCand = 15u;
                        else
                            skyCand = ns > 0u ? static_cast<u8>(ns - 1u) : 0u;
                        const u8 blkCand = nb > 0u ? static_cast<u8>(nb - 1u) : 0u;
                        if (skyCand > sky)
                            sky = skyCand;
                        if (blkCand > blk)
                            blk = blkCand;
                    }
                }
                I.lightMirror[libase + vi] = static_cast<u8>((sky << 4u) | (blk & 0xFu));
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
        // Edited brickEx (voxels + flat light) is re-staged in Update with
        // 1-frame async lag; the GPU edit-shader path covers occupancy now.
        {
            const Impl::EditRestage_t e{Impl::Key(bsx, bsy, bsz),
                                        static_cast<u32>(it->second.brickIdx)};
            bool dup = false;
            for (const auto &q : I.editRestage) {
                if (q.key == e.key) {
                    dup = true;
                    break;
                }
            }
            if (!dup)
                I.editRestage.push_back(e);
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
