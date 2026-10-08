// CVoxelStreamWorld: save-backed section fill for the voxel renderer.
// Sections are 16^3 blocks (== our bricks). World data comes from an Anvil
// save on disk (region files under worldDir); there is no procedural
// fallback — without a save the volume fills as air and Init reports an
// error. Block textures come from the build-time client-jar assets.

#include "VoxelStreamWorld.h"
#include "VoxelAnvilReader.h"
#include "VoxelBlockAssets.h"
#include "VoxelWorld.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Manro {
    namespace {
        constexpr int kMinSectionY = -4; // y -64
        constexpr int kMaxSectionY = 19; // y ..319
    } // namespace

    struct CVoxelStreamWorld::Impl {
        VoxelStreamDesc_t desc{};
        const BlockAssetPack_t *pack{nullptr};
        CAnvilWorldReader reader;
        bool hasSave{false};
        // Live sections: world-section key -> slot state.
        struct SlotInfo {
            i32 brickIdx{-1};
            bool filled{false};
        };
        std::unordered_map<i64, SlotInfo> slots;
        // Sections wanted but not yet filled (shrinks as streaming catches
        // up; rebuilt when the streaming center moves).
        std::vector<i64> unfilled;
        // Streaming center (player section XZ) + eviction radius (R+1 when
        // the hysteresis ring fits the resident pool, else R).
        int centerCx{0}, centerCz{0};
        bool hasCenter{false};
        int evictRadius{7};
        // worldMin in brick units (fixed at Init; shaders wrap slots
        // mod-virtualDim against the same origin, so it never moves).
        int minBX{0}, minBZ{0};
        // CPU mirrors for physics/raycasts: occupancy + fluid bits per brick
        // (128 u32 words each, indexed brickIdx*128). Unfilled/missing reads
        // as solid (players can't fall through unloaded world).
        std::vector<u32> occMirror;
        std::vector<u32> fluidMirror;
        // CPU state mirror: block state id per voxel (4096 u16 per brick,
        // indexed brickIdx*4096 + vi). Powers pick-block + oriented
        // placement; kept in lockstep with occMirror on fill/edit/evict.
        std::vector<u16> stateMirror;
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

        // Page-table slot for a world section. Matches the shaders, which
        // wrap floor((worldPos-worldMin)/brickSize) mod-virtualDim.
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

        // Physics/raycast queries against the CPU mirrors. Unfilled or
        // untracked sections read SOLID (nothing may fall through unloaded
        // world); out-of-range Y is bedrock/air respectively.
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

        // Recompute the opaque-full flag from the state mirror after a
        // gameplay edit. Breaking a block inside solid rock MUST clear it:
        // a stale 1 poisons later RecomputeHidden calls when neighboring
        // sections stream in, hiding bricks whose boundary faces are
        // actually visible through the cavity — whole-block rectangular
        // see-through holes, visible only from the cavity side and immune
        // to the B (backface) / N (frustum) kill switches. Predicate matches
        // the fill-time allOpaque computation exactly.
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

        // Block state id at a voxel, or -1 when the section isn't filled
        // (pick-block treats unknown as invalid, never as air).
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
            // No procedural fallback: without a save the volume fills as air.
            I.spawn = Vec3(8.f, 80.f, 8.f);
            std::printf("[StreamWorld] ERROR: no region data in '%s' (pass --world-dir)\n",
                        desc.worldDir.c_str());
        }

        // Streaming world: the origin is fixed ONCE (shaders wrap slots
        // mod-virtualDim against it, so it must never move), sections are
        // allocated/filled around the player in Update and evicted past the
        // hysteresis ring. Nothing is allocated here besides sparse pages.
        const int csx = static_cast<int>(std::floor(I.spawn.x / 16.f));
        const int csz = static_cast<int>(std::floor(I.spawn.z / 16.f));
        const int R = desc.radiusSections;
        world.SetWorldMin(Vec3(static_cast<float>((csx - R) * 16), -64.f,
                               static_cast<float>((csz - R) * 16)));
        I.minBX = csx - R;
        I.minBZ = csz - R;
        // Hysteresis ring fits the pool? (2(R+1)+1)^2 columns x 24 sections.
        const int wantLive =
            (2 * (R + 1) + 1) * (kMaxSectionY - kMinSectionY + 1) * (2 * (R + 1) + 1);
        I.evictRadius = (wantLive <= 8192) ? (R + 1) : R;
        // Bind the whole resident pool up front in ONE queue operation, so
        // steady-state streaming (alloc reuse + fills) never touches the
        // sparse bind queue. Without this each brick binds + drains the
        // queue on its own (~580 waits, ~300ms at R=6).
        world.ReserveResident(8192);
        std::printf("[StreamWorld] streaming R=%d evict=%d save=%d spawn=(%.1f,%.1f,%.1f)\n", R,
                    I.evictRadius, I.hasSave ? 1 : 0, I.spawn.x, I.spawn.y, I.spawn.z);
        return I.spawn;
    }

    int CVoxelStreamWorld::UnfilledCount() const {
        return static_cast<int>(m_Impl->unfilled.size());
    }

    int CVoxelStreamWorld::Update(CVoxelWorld &world, const Vec3 &cameraPos, u32 flightSlot) {
        auto &I = *m_Impl;
        const int R = I.desc.radiusSections;
        const auto secOf = [](float v) { return static_cast<int>(std::floor(v / 16.f)); };
        const int pcx = secOf(cameraPos.x);
        const int pcy = secOf(cameraPos.y);
        const int pcz = secOf(cameraPos.z);

        // Follow the player: when the center section changes, evict live
        // sections outside the hysteresis ring and rebuild the wanted set.
        // Eviction returns brick indices to the reuse pool (no unbind cost);
        // the free pool + full-height columns keep this O(volume) rarely.
        if (!I.hasCenter || pcx != I.centerCx || pcz != I.centerCz) {
            I.centerCx = pcx;
            I.centerCz = pcz;
            I.hasCenter = true;
            const int E = I.evictRadius;
            for (auto it = I.slots.begin(); it != I.slots.end();) {
                int sx, sy, sz;
                Impl::DecodeKey(it->first, sx, sy, sz);
                if (std::max(std::abs(sx - pcx), std::abs(sz - pcz)) > E) {
                    const i32 b = it->second.brickIdx;
                    if (b >= 0) {
                        world.EvictBrick(static_cast<u32>(b));
                        const size_t base = static_cast<size_t>(b) * 128;
                        if (base + 128 <= I.occMirror.size()) {
                            std::fill(I.occMirror.begin() + static_cast<ptrdiff_t>(base),
                                      I.occMirror.begin() + static_cast<ptrdiff_t>(base + 128), 0u);
                            std::fill(I.fluidMirror.begin() + static_cast<ptrdiff_t>(base),
                                      I.fluidMirror.begin() + static_cast<ptrdiff_t>(base + 128),
                                      0u);
                        }
                        const size_t sbase = static_cast<size_t>(b) * 4096;
                        if (sbase + 4096 <= I.stateMirror.size()) {
                            std::fill(I.stateMirror.begin() + static_cast<ptrdiff_t>(sbase),
                                      I.stateMirror.begin() + static_cast<ptrdiff_t>(sbase + 4096),
                                      0u);
                        }
                    }
                    it = I.slots.erase(it);
                } else {
                    ++it;
                }
            }
            I.unfilled.clear();
            I.unfilled.reserve(static_cast<size_t>(2 * R + 1) * 24 * (2 * R + 1));
            for (int sz = pcz - R; sz <= pcz + R; ++sz) {
                for (int sy = kMinSectionY; sy <= kMaxSectionY; ++sy) {
                    for (int sx = pcx - R; sx <= pcx + R; ++sx) {
                        const i64 key = Impl::Key(sx, sy, sz);
                        const auto it = I.slots.find(key);
                        if (it == I.slots.end() || !it->second.filled)
                            I.unfilled.push_back(key);
                    }
                }
            }
            // Nearest-first ONCE per rebuild. The old per-frame path (heap
            // alloc + nth_element + partial sort of ~4k candidates every
            // frame) was hot during streaming; the order goes slightly stale
            // as the camera drifts inside the center section, which only
            // costs fill order, not correctness.
            std::sort(I.unfilled.begin(), I.unfilled.end(), [&](i64 a, i64 b) {
                int ax, ay, az, bx, by, bz;
                Impl::DecodeKey(a, ax, ay, az);
                Impl::DecodeKey(b, bx, by, bz);
                const float adx = static_cast<float>(ax - pcx), ady = static_cast<float>(ay - pcy),
                            adz = static_cast<float>(az - pcz);
                const float bdx = static_cast<float>(bx - pcx), bdy = static_cast<float>(by - pcy),
                            bdz = static_cast<float>(bz - pcz);
                return adx * adx + ady * ady + adz * adz < bdx * bdx + bdy * bdy + bdz * bdz;
            });
        }
        // Streaming caught up: no scan, no sort, no allocation — O(1).
        if (I.unfilled.empty())
            return 0;
        // Pre-sorted at rebuild: take from the front, no per-frame work.
        const int budget = std::min<int>(I.desc.fillBudgetPerUpdate,
                                         static_cast<int>(I.unfilled.size()));
        // Deferred device upload: StreamUpdate only stages into the flight-slot
        // ring (plain memcpys); Record emits the copies into the frame CB
        // with a transfer barrier — no one-shot submit, no fence wait, no
        // pipeline drain while streaming.
        thread_local std::vector<u16> batchMats;
        thread_local std::vector<u32> batchOcc;
        thread_local std::vector<u32> batchFluid;
        thread_local std::vector<u32> batchGpuOcc;
        thread_local std::vector<i64> filledKeys;
        batchMats.resize(static_cast<size_t>(budget) * 4096);
        batchOcc.resize(static_cast<size_t>(budget) * 128);
        batchFluid.resize(static_cast<size_t>(budget) * 128);
        batchGpuOcc.resize(static_cast<size_t>(budget) * 128);
        std::vector<u32> batchIdx;
        batchIdx.reserve(static_cast<size_t>(budget));
        std::vector<u8> batchOpaque;
        batchOpaque.reserve(static_cast<size_t>(budget));
        filledKeys.clear();
        AnvilSectionStates saveSec{};
        // Per-section palette -> pack id translation (palettes are tiny).
        std::vector<u32> palLocal;
        for (int i = 0; i < budget; ++i) {
            const i64 key = I.unfilled[static_cast<size_t>(i)];
            int sx, sy, sz;
            Impl::DecodeKey(key, sx, sy, sz);
            auto sit = I.slots.find(key);
            u32 bidx = ~0u;
            if (sit == I.slots.end()) {
                // Allocate (reuse pool first, high-water otherwise). Pool
                // exhausted: skip this frame, retry after evictions.
                const i32 nb =
                    world.AllocateBrickSlot(I.SlotFor(sx, sy, sz), Impl::OriginFor(sx, sy, sz));
                if (nb < 0)
                    break; // pool exhausted: stop the batch, retry after evictions
                sit = I.slots.emplace(key, Impl::SlotInfo{nb, false}).first;
                bidx = static_cast<u32>(nb);
            } else if (sit->second.filled || sit->second.brickIdx < 0) {
                continue;
            } else {
                bidx = static_cast<u32>(sit->second.brickIdx);
            }
            u16 *mats = batchMats.data() + static_cast<size_t>(batchIdx.size()) * 4096;
            u32 *occ = batchOcc.data() + static_cast<size_t>(batchIdx.size()) * 128;
            u32 *fld = batchFluid.data() + static_cast<size_t>(batchIdx.size()) * 128;
            std::fill(occ, occ + 128, 0u);
            std::fill(fld, fld + 128, 0u);
            bool allOpaque = true;
            // Save-backed fill: section palette entries resolve through the
            // pack table (built from this same save, so hits are the norm).
            // Missing data (ungenerated chunk) fills as air.
            palLocal.clear();
            bool haveData = false;
            if (I.hasSave && I.reader.ReadSectionStates(sx, sy, sz, saveSec) &&
                saveSec.present) {
                haveData = true;
                palLocal.reserve(saveSec.palette.size());
                for (const FullState &fs : saveSec.palette) {
                    const auto it =
                        I.pack->stateByKey.find(MakeStateKey(fs.name, fs.props));
                    palLocal.push_back(it != I.pack->stateByKey.end()
                                            ? it->second
                                            : I.pack->fallbackState);
                }
            }
            for (int vi = 0; vi < 4096; ++vi) {
                const u32 st = haveData ? palLocal[saveSec.pal[vi]] : 0u;
                const bool valid = static_cast<size_t>(st) < I.pack->states.size();
                const u32 flags = valid ? I.pack->states[static_cast<size_t>(st)].flags : 0u;
                const bool skip = !valid || ((flags & kBlockFlagSkip) != 0);
                mats[vi] = skip ? 0 : static_cast<u16>(st);
                if (!skip) {
                    // Physics mirror: fluids are swimmable, NOT solid
                    // (the GPU brick occupancy keeps them for rendering;
                    // only this CPU mirror drives collision/raycasts).
                    const bool fluid = static_cast<size_t>(st) < I.pack->fluid.size() &&
                                       I.pack->fluid[static_cast<size_t>(st)] != 0u;
                    if (fluid)
                        fld[vi >> 5u] |= (1u << (vi & 31u));
                    else
                        occ[vi >> 5u] |= (1u << (vi & 31u));
                }
                // Fully-opaque brick tracking (hidden-brick culling):
                // any air/skip/cutout/non-opaque voxel disqualifies.
                if (skip || (flags & kBlockFlagOpaque) == 0u)
                    allOpaque = false;
            }
            batchIdx.push_back(bidx);
            batchOpaque.push_back(allOpaque ? 1u : 0u);
            sit->second.filled = true;
            filledKeys.push_back(key);
        }
        if (!batchIdx.empty()) {
            // GPU occupancy must include fluids (they render as opaque
            // water/lava tiles); the CPU physics mirror below stays
            // solid-only (fluids are swimmable, not solid). Without the OR
            // here water bricks scan as empty on the GPU: fully invisible
            // while physics still reports solid/air correctly.
            for (size_t k = 0; k < batchIdx.size(); ++k) {
                const u32 *occ = batchOcc.data() + k * 128;
                const u32 *fld = batchFluid.data() + k * 128;
                u32 *gpu = batchGpuOcc.data() + k * 128;
                for (int w = 0; w < 128; ++w)
                    gpu[w] = occ[w] | fld[w];
            }
            if (!world.StageBrickBatch(flightSlot, batchIdx.data(), batchMats.data(),
                                       batchGpuOcc.data(), static_cast<u32>(batchIdx.size()))) {
                // Staging ring full (budget <= 32 < 64 capacity: unreachable
                // in practice): synchronous fallback with a pipeline drain.
                world.UploadBrickBatch(batchIdx.data(), batchMats.data(), batchGpuOcc.data(),
                                       static_cast<u32>(batchIdx.size()));
            }
            for (size_t k = 0; k < batchIdx.size(); ++k) {
                // Physics mirrors (occupancy + fluid) for the filled brick.
                const size_t need = (static_cast<size_t>(batchIdx[k]) + 1) * 128;
                if (I.occMirror.size() < need) {
                    I.occMirror.resize(need, 0u);
                    I.fluidMirror.resize(need, 0u);
                }
                std::memcpy(I.occMirror.data() + static_cast<size_t>(batchIdx[k]) * 128,
                            batchOcc.data() + k * 128, sizeof(u32) * 128);
                std::memcpy(I.fluidMirror.data() + static_cast<size_t>(batchIdx[k]) * 128,
                            batchFluid.data() + k * 128, sizeof(u32) * 128);
                // State mirror (pick-block + oriented placement).
                const size_t sneed = (static_cast<size_t>(batchIdx[k]) + 1) * 4096;
                if (I.stateMirror.size() < sneed)
                    I.stateMirror.resize(sneed, 0u);
                std::memcpy(I.stateMirror.data() + static_cast<size_t>(batchIdx[k]) * 4096,
                            batchMats.data() + k * 4096, sizeof(u16) * 4096);
                world.SetBrickOpaqueFull(batchIdx[k], batchOpaque[k] != 0u);
                // Re-evaluate neighbor boundaries too: this fill can hide
                // (or, for air pockets, reveal) adjacent bricks' faces.
                world.MarkBrickAndNeighborsDirty(batchIdx[k]);
            }
        }
        if (!filledKeys.empty()) {
            // Swap-remove filled keys from the unfilled list (single pass,
            // binary search over the tiny sorted filled set).
            std::sort(filledKeys.begin(), filledKeys.end());
            size_t w = 0;
            for (size_t r = 0; r < I.unfilled.size(); ++r) {
                if (!std::binary_search(filledKeys.begin(), filledKeys.end(), I.unfilled[r]))
                    I.unfilled[w++] = I.unfilled[r];
            }
            I.unfilled.resize(w);
        }
        // DEBUG-PROBE: camera column top surface from the occupancy mirror.
        {
            static u32 probeSeq = 0;
            if ((++probeSeq % 90) == 1) {
                const int px = static_cast<int>(std::floor(cameraPos.x));
                const int pz = static_cast<int>(std::floor(cameraPos.z));
                int mirrorTop = -999;
                for (int y = 100; y >= -64; --y) {
                    if (I.IsSolidAt(px, y, pz)) {
                        mirrorTop = y;
                        break;
                    }
                }
                std::printf("[StreamWorld][probe] cam=(%d,%d) mirrorTop=%d unfilled=%zu\n", px,
                            pz, mirrorTop, I.unfilled.size());
                if (!I.unfilled.empty() && I.unfilled.size() <= 12) {
                    for (const i64 k : I.unfilled) {
                        int a, b, c;
                        Impl::DecodeKey(k, a, b, c);
                        const auto it2 = I.slots.find(k);
                        std::printf("[StreamWorld][stuck] key=(%d,%d,%d) slotEntry=%d filled=%d\n",
                                    a, b, c, it2 == I.slots.end() ? -2 : it2->second.brickIdx,
                                    it2 == I.slots.end() ? 0 : (it2->second.filled ? 1 : 0));
                    }
                }
            }
        }
        return static_cast<int>(I.unfilled.size());
    }

    void CVoxelStreamWorld::ApplyEdit(CVoxelWorld &world, const Vec3 &pos, float radius, u32 op,
                                      u32 state) {
        auto &I = *m_Impl;
        // Home brick only (mirrors the GPU edit shader's single-brick span).
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
            // Clamp to the home brick (out-of-brick voxels belong to a
            // neighbor the GPU edit never touches).
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
                I.fluidMirror[w] &= ~bit; // placed states are solid, never fluid
                if (hasState)
                    I.stateMirror[sbase + vi] = static_cast<u16>(state & 0xFFFFu);
            }
        };
        if (radius < 0.5f) {
            applyVoxel(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)),
                       static_cast<int>(std::floor(pos.z)));
            I.RecomputeOpaque(world, it->second.brickIdx);
            return;
        }
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
                    // Same sphere predicate as the GPU edit shader.
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

    bool CVoxelStreamWorld::IsSolidAt(i64 x, i64 y, i64 z) const { return m_Impl->IsSolidAt(x, y, z); }

    bool CVoxelStreamWorld::IsFluidAt(i64 x, i64 y, i64 z) const { return m_Impl->IsFluidAt(x, y, z); }

    i32 CVoxelStreamWorld::GetStateAt(i64 x, i64 y, i64 z) const { return m_Impl->StateAt(x, y, z); }

    i32 CVoxelStreamWorld::PlaceState() const {
        return static_cast<i32>(m_Impl->pack->placeState);
    }
} // namespace Manro
