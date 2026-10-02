// CVoxelMcWorld: MC-dimension section fill (Anvil via mcs, procedural
// fallback). This TU is C++23 (mcs headers) — see engine CMake.

#include "VoxelMcWorld.h"
#include "VoxelMcAssets.h"
#include "VoxelWorld.h"

#include "mcs/world/anvil.hpp"
#include "mcs/protocol/block_states.hpp"
#include "mcs/protocol/play/constants.hpp"

#define STB_PERLIN_IMPLEMENTATION
#include <stb_perlin.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Manro {
    namespace play = mcs::protocol::play;

    namespace {
        constexpr int kMinSectionY = -4; // y -64
        constexpr int kMaxSectionY = 19; // y ..319
        constexpr int kSeaLevel = 62;

        float Fbm(float x, float z) {
            float a = 0.5f, f = 1.f / 180.f, s = 0.f, norm = 0.f;
            for (int o = 0; o < 4; ++o) {
                s += a * stb_perlin_noise3(x * f, 0.f, z * f, 0, 0, 0);
                norm += a;
                a *= 0.5f;
                f *= 2.03f;
            }
            return s / norm; // ~[-1, 1]
        }

        u64 Hash2(i64 x, i64 z) {
            u64 h = static_cast<u64>(x * 374761393LL + z * 668265263LL);
            h = (h ^ (h >> 13)) * 1274126177ULL;
            return h ^ (h >> 16);
        }
    } // namespace

    struct CVoxelMcWorld::Impl {
        McWorldDesc_t desc{};
        std::shared_ptr<const mcs::world::AnvilWorld> anvil;
        const McAssetPack_t *pack{nullptr};
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
        // Fluid protocol states (water/lava levels) for the fluid mirror.
        std::vector<u8> fluidState;
        Vec3 spawn{8.f, 80.f, 8.f};
        // Procedural state ids (resolved once).
        i32 sGrass{1}, sDirt{1}, sStone{1}, sBedrock{1}, sWater{0}, sLog{1}, sLeaves{0}, sSand{1};
        i32 sPlace{1}; // right-click place block (planks, stone fallback)

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

        i32 Procedural(i64 x, i64 y, i64 z) {
            if (y < -64)
                return sBedrock; // floor guard (shouldn't happen)
            if (y == -64)
                return sBedrock;
            const float h = 66.f + Fbm(static_cast<float>(x), static_cast<float>(z)) * 22.f;
            // Trees: hash grid, trunks on grass.
            const i64 tx = x >> 4, tz = z >> 4;
            const u64 th = Hash2(tx, tz);
            const int treeX = static_cast<int>(tx * 16 + (th % 11));
            const int treeZ = static_cast<int>(tz * 16 + ((th >> 4) % 11));
            const float treeH = 66.f + Fbm(static_cast<float>(treeX), static_cast<float>(treeZ)) * 22.f;
            if ((th & 7) == 0 && x >= treeX - 2 && x <= treeX + 2 && z >= treeZ - 2 &&
                z <= treeZ + 2) {
                const int ty = static_cast<int>(treeH);
                const int dx = static_cast<int>(x - treeX), dz = static_cast<int>(z - treeZ);
                const int ad = std::abs(dx) + std::abs(dz);
                if (y > ty && y <= ty + 4 && (dx != 0 || dz != 0) && ad <= 4 &&
                    !(std::abs(dx) == 2 && std::abs(dz) == 2))
                    return sLeaves;
                if (y == ty + 5 && dx == 0 && dz == 0)
                    return sLeaves;
                if (y > ty && y <= ty + 4 && dx == 0 && dz == 0)
                    return sLog;
            }
            if (y > static_cast<i64>(h))
                return (y <= kSeaLevel) ? sWater : 0;
            if (y == static_cast<i64>(h)) {
                if (h <= kSeaLevel + 1)
                    return sSand; // beaches
                return sGrass;
            }
            if (y > static_cast<i64>(h) - 4)
                return (h <= kSeaLevel + 1) ? sSand : sDirt;
            return sStone;
        }
        // Terrain half of Procedural() for a precomputed column (h/hi known).
        // Bit-exact with Procedural() for y > -64 outside tree crowns.
        i32 ColumnTerrain(i64 y, float h, i64 hi) {
            if (y > hi)
                return (y <= kSeaLevel) ? sWater : 0;
            if (y == hi) {
                if (h <= kSeaLevel + 1)
                    return sSand; // beaches
                return sGrass;
            }
            if (y > hi - 4)
                return (h <= kSeaLevel + 1) ? sSand : sDirt;
            return sStone;
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
    };

    CVoxelMcWorld::CVoxelMcWorld() : m_Impl(std::make_unique<Impl>()) {}
    CVoxelMcWorld::~CVoxelMcWorld() = default;

    Vec3 CVoxelMcWorld::Init(CVoxelWorld &world, const McAssetPack_t &pack,
                             const McWorldDesc_t &desc) {
        m_Impl->desc = desc;
        m_Impl->pack = &pack;
        auto res = [](const char *name,
                      std::initializer_list<std::pair<std::string_view, std::string_view>> p) {
            std::vector<std::pair<std::string_view, std::string_view> > v(p);
            return play::resolve_block_state(name, v);
        };
        auto &I = *m_Impl;
        I.sGrass = res("minecraft:grass_block", {{"snowy", "false"}});
        I.sDirt = res("minecraft:dirt", {});
        I.sStone = res("minecraft:stone", {});
        I.sBedrock = res("minecraft:bedrock", {});
        I.sWater = res("minecraft:water", {{"level", "0"}});
        I.sLog = res("minecraft:oak_log", {{"axis", "y"}});
        I.sLeaves = res("minecraft:oak_leaves", {{"distance", "1"},
                                                 {"persistent", "true"},
                                                 {"waterlogged", "false"}});
        I.sSand = res("minecraft:sand", {});
        if (I.sLeaves < 0)
            I.sLeaves = 0;
        I.sPlace = res("minecraft:oak_planks", {});
        if (I.sPlace < 0)
            I.sPlace = I.sStone;
        // Fluid states (every water/lava level) for the physics fluid mirror.
        I.fluidState.assign(I.pack->states.size(), 0u);
        for (int lv = 0; lv <= 15; ++lv) {
            const std::string lvs = std::to_string(lv);
            const i32 w = res("minecraft:water", {{"level", lvs}});
            const i32 l = res("minecraft:lava", {{"level", lvs}});
            if (w >= 0 && static_cast<size_t>(w) < I.fluidState.size())
                I.fluidState[static_cast<size_t>(w)] = 1u;
            if (l >= 0 && static_cast<size_t>(l) < I.fluidState.size())
                I.fluidState[static_cast<size_t>(l)] = 1u;
        }

        if (!desc.worldDir.empty()) {
            auto anvil = mcs::world::AnvilWorld::open(desc.worldDir);
            if (anvil) {
                I.anvil = anvil;
                const auto &lv = anvil->level();
                I.spawn = Vec3(static_cast<float>(lv.spawn_x), static_cast<float>(lv.spawn_y) + 1.f,
                               static_cast<float>(lv.spawn_z));
                std::printf("[McWorld] anvil %s spawn=(%d,%d,%d)\n", desc.worldDir.c_str(),
                         lv.spawn_x, lv.spawn_y, lv.spawn_z);
            } else {
                std::printf("[McWorld] no region data in %s, procedural fallback\n",
                         desc.worldDir.c_str());
            }
        }
        if (!I.anvil) {
            // Procedural spawn above terrain at origin.
            const float h = 66.f + Fbm(8.f, 8.f) * 22.f;
            I.spawn = Vec3(8.f, h + 2.f, 8.f);
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
        std::printf("[McWorld] streaming R=%d evict=%d anvil=%d spawn=(%.1f,%.1f,%.1f)\n", R,
                 I.evictRadius, I.anvil ? 1 : 0, I.spawn.x, I.spawn.y, I.spawn.z);
        return I.spawn;
    }

    int CVoxelMcWorld::UnfilledCount() const {
        return static_cast<int>(m_Impl->unfilled.size());
    }

    int CVoxelMcWorld::Update(CVoxelWorld &world, const Vec3 &cameraPos, u32 flightSlot) {
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
        // Deferred device upload: McUpdate only stages into the flight-slot
        // ring (plain memcpys); Record emits the copies into the frame CB
        // with a transfer barrier — no one-shot submit, no fence wait, no
        // pipeline drain while streaming.
        thread_local std::vector<u16> batchMats;
        thread_local std::vector<u32> batchOcc;
        thread_local std::vector<u32> batchFluid;
        thread_local std::vector<i64> filledKeys;
        batchMats.resize(static_cast<size_t>(budget) * 4096);
        batchOcc.resize(static_cast<size_t>(budget) * 128);
        batchFluid.resize(static_cast<size_t>(budget) * 128);
        std::vector<u32> batchIdx;
        batchIdx.reserve(static_cast<size_t>(budget));
        std::vector<u8> batchOpaque;
        batchOpaque.reserve(static_cast<size_t>(budget));
        filledKeys.clear();
        const size_t fluidStates = I.fluidState.size();
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
            // Anvil fast path: one chunk fetch per section (sections align
            // 1:1 with chunk columns x chunk sections), then lock-free reads.
            // Per-voxel block_state_at costs a mutex + hash lookup +
            // shared_ptr atomic each — ~4096x overhead per section.
            std::shared_ptr<const mcs::world::LoadedChunk> chunk;
            const mcs::world::ChunkSection *sec = nullptr;
            if (I.anvil) {
                chunk = I.anvil->chunk(sx, sz);
                const int secIdx = sy + 4;
                if (chunk && secIdx >= 0 &&
                    static_cast<size_t>(secIdx) < chunk->sections.size())
                    sec = &chunk->sections[static_cast<size_t>(secIdx)];
            }
            // Column-major fill: terrain height + tree params depend only on
            // (x,z), so they are computed once per column (256x) instead of
            // per voxel (4096x). Fbm/Hash are pure in (x,z), so hoisting is
            // exact. The Anvil path can't hoist (per-voxel lookups).
            for (int lz = 0; lz < 16; ++lz) {
                for (int lx = 0; lx < 16; ++lx) {
                    const i64 x = static_cast<i64>(sx) * 16 + lx;
                    const i64 z = static_cast<i64>(sz) * 16 + lz;
                    float colH = 0.f;
                    i64 colHi = 0;
                    bool colTree = false;
                    int colTreeX = 0, colTreeZ = 0, colTreeY = 0;
                    if (!I.anvil) {
                        colH = 66.f + Fbm(static_cast<float>(x), static_cast<float>(z)) * 22.f;
                        colHi = static_cast<i64>(colH);
                        const i64 tx = x >> 4, tz = z >> 4;
                        const u64 th = Hash2(tx, tz);
                        colTreeX = static_cast<int>(tx * 16 + (th % 11));
                        colTreeZ = static_cast<int>(tz * 16 + ((th >> 4) % 11));
                        const float treeH =
                            66.f +
                            Fbm(static_cast<float>(colTreeX), static_cast<float>(colTreeZ)) * 22.f;
                        colTreeY = static_cast<int>(treeH);
                        colTree = ((th & 7) == 0 && x >= colTreeX - 2 && x <= colTreeX + 2 &&
                                   z >= colTreeZ - 2 && z <= colTreeZ + 2);
                    }
                    for (int ly = 0; ly < 16; ++ly) {
                        const i64 y = static_cast<i64>(sy) * 16 + ly;
                        i32 st = 0;
                        if (sec) {
                            const size_t cell =
                                static_cast<size_t>(ly * 256 + lz * 16 + lx);
                            st = sec->block_state(cell);
                        } else if (I.anvil) {
                            st = 0; // missing chunk/section reads as air
                        } else if (y <= -64) {
                            st = I.sBedrock;
                        } else if (colTree) {
                            const int dx = static_cast<int>(x - colTreeX);
                            const int dz = static_cast<int>(z - colTreeZ);
                            const int ad = std::abs(dx) + std::abs(dz);
                            if (y > colTreeY && y <= colTreeY + 4 && (dx != 0 || dz != 0) &&
                                ad <= 4 && !(std::abs(dx) == 2 && std::abs(dz) == 2))
                                st = I.sLeaves;
                            else if (y == colTreeY + 5 && dx == 0 && dz == 0)
                                st = I.sLeaves;
                            else if (y > colTreeY && y <= colTreeY + 4 && dx == 0 && dz == 0)
                                st = I.sLog;
                            else
                                st = I.ColumnTerrain(y, colH, colHi);
                        } else {
                            st = I.ColumnTerrain(y, colH, colHi);
                        }
                        const size_t vi = static_cast<size_t>(lx + ly * 16 + lz * 256);
                        const bool valid = st >= 0 &&
                                           static_cast<size_t>(st) < I.pack->states.size();
                        const u32 flags =
                            valid ? I.pack->states[static_cast<size_t>(st)].flags : 0u;
                        const bool skip =
                            !valid || ((flags & kMcFlagSkip) != 0);
                        mats[vi] = skip ? 0 : static_cast<u16>(st);
                        if (!skip) {
                            // Physics mirror: fluids are swimmable, NOT solid
                            // (the GPU brick occupancy keeps them for rendering;
                            // only this CPU mirror drives collision/raycasts).
                            const bool fluid = static_cast<size_t>(st) < fluidStates &&
                                               I.fluidState[static_cast<size_t>(st)] != 0u;
                            if (fluid)
                                fld[vi >> 5u] |= (1u << (vi & 31u));
                            else
                                occ[vi >> 5u] |= (1u << (vi & 31u));
                        }
                        // Fully-opaque brick tracking (hidden-brick culling):
                        // any air/skip/cutout/non-opaque voxel disqualifies.
                        if (skip || (flags & kMcFlagOpaque) == 0u)
                            allOpaque = false;
                    }
                }
            }
            batchIdx.push_back(bidx);
            batchOpaque.push_back(allOpaque ? 1u : 0u);
            sit->second.filled = true;
            filledKeys.push_back(key);
        }
        if (!batchIdx.empty()) {
            if (!world.StageBrickBatch(flightSlot, batchIdx.data(), batchMats.data(),
                                       batchOcc.data(), static_cast<u32>(batchIdx.size()))) {
                // Staging ring full (budget <= 32 < 64 capacity: unreachable
                // in practice): synchronous fallback with a pipeline drain.
                world.UploadBrickBatch(batchIdx.data(), batchMats.data(), batchOcc.data(),
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
        // DEBUG-PROBE: camera column surface from the occupancy mirror vs
        // a fresh Fbm eval — catches CPU/GPU brick contamination.
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
                const float fh = 66.f + Fbm(static_cast<float>(px), static_cast<float>(pz)) * 22.f;
                std::printf("[McWorld][probe] cam=(%d,%d) mirrorTop=%d fbmH=%.1f unfilled=%zu\n",
                            px, pz, mirrorTop, static_cast<double>(fh), I.unfilled.size());
                if (!I.unfilled.empty() && I.unfilled.size() <= 12) {
                    for (const i64 k : I.unfilled) {
                        int a, b, c;
                        Impl::DecodeKey(k, a, b, c);
                        const auto it2 = I.slots.find(k);
                        std::printf("[McWorld][stuck] key=(%d,%d,%d) slotEntry=%d filled=%d\n", a, b,
                                    c, it2 == I.slots.end() ? -2 : it2->second.brickIdx,
                                    it2 == I.slots.end() ? 0 : (it2->second.filled ? 1 : 0));
                    }
                }
            }
        }
        return static_cast<int>(I.unfilled.size());
    }

    bool CVoxelMcWorld::HasAnvil() const { return m_Impl->anvil != nullptr; }

    void CVoxelMcWorld::ApplyEdit(const Vec3 &pos, float radius, u32 op) {
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
            } else {
                I.occMirror[w] |= bit;
                I.fluidMirror[w] &= ~bit; // placed states are solid, never fluid
            }
        };
        if (radius < 0.5f) {
            applyVoxel(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)),
                       static_cast<int>(std::floor(pos.z)));
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
    }

    bool CVoxelMcWorld::IsSolidAt(i64 x, i64 y, i64 z) const { return m_Impl->IsSolidAt(x, y, z); }

    bool CVoxelMcWorld::IsFluidAt(i64 x, i64 y, i64 z) const { return m_Impl->IsFluidAt(x, y, z); }

    i32 CVoxelMcWorld::PlaceState() const { return m_Impl->sPlace; }
} // namespace Manro
