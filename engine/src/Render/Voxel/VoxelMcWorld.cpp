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
        // Section key -> slot state. Slot (bx,by,bz) in world-local coords.
        struct SlotInfo {
            i32 brickIdx{-1};
            bool filled{false};
        };
        std::unordered_map<i64, SlotInfo> slots;
        // Unfilled section keys (shrinks as streaming completes; empty =>
        // Update returns 0 immediately without scanning anything).
        std::vector<i64> unfilled;
        Vec3 spawn{8.f, 80.f, 8.f};
        // Procedural state ids (resolved once).
        i32 sGrass{1}, sDirt{1}, sStone{1}, sBedrock{1}, sWater{0}, sLog{1}, sLeaves{0}, sSand{1};

        static i64 Key(int sx, int sy, int sz) {
            return (static_cast<i64>(sx + 32768) << 42) | (static_cast<i64>(sy + 32768) << 21) |
                   static_cast<i64>(sz + 32768);
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

        // Allocate the full static volume now; fill lazily in Update.
        // Slot coords are volume-local: bx = sx-csx+R, by = sy+4, bz = sz-csz+R,
        // so the world origin must be the volume min corner (set before alloc;
        // origins are alloc-time constants).
        const int csx = static_cast<int>(std::floor(I.spawn.x / 16.f));
        const int csz = static_cast<int>(std::floor(I.spawn.z / 16.f));
        const int R = desc.radiusSections;
        world.SetWorldMin(Vec3(static_cast<float>((csx - R) * 16), -64.f,
                               static_cast<float>((csz - R) * 16)));
        // Bind all sparse pages up front in ONE queue operation. Without
        // this each AllocateBrick below binds + drains the queue on its own
        // (~580 waits, ~300ms at R=6).
        world.ReserveResident(
            static_cast<u32>((2 * R + 1) * (kMaxSectionY - kMinSectionY + 1) * (2 * R + 1)));
        for (int sz = csz - R; sz <= csz + R; ++sz) {
            for (int sy = kMinSectionY; sy <= kMaxSectionY; ++sy) {
                for (int sx = csx - R; sx <= csx + R; ++sx) {
                    const u32 bx = static_cast<u32>(sx - csx + R);
                    const u32 by = static_cast<u32>(sy + 4);
                    const u32 bz = static_cast<u32>(sz - csz + R);
                    const i32 idx = world.AllocateBrick(bx, by, bz);
                    if (idx >= 0)
                        I.slots[Impl::Key(sx, sy, sz)] = Impl::SlotInfo{idx, false};
                }
            }
        }
        std::printf("[McWorld] allocated %zu sections R=%d anvil=%d\n", I.slots.size(), R,
                 I.anvil ? 1 : 0);
        I.unfilled.reserve(I.slots.size());
        for (const auto &[key, s] : I.slots)
            I.unfilled.push_back(key);
        return I.spawn;
    }

    int CVoxelMcWorld::UnfilledCount() const {
        return static_cast<int>(m_Impl->unfilled.size());
    }

    int CVoxelMcWorld::Update(CVoxelWorld &world, const Vec3 &cameraPos) {
        auto &I = *m_Impl;
        // Streaming complete: no scan, no sort, no allocation — O(1).
        // (The old code rebuilt + nth_element'd a 4k candidate list every
        // frame even after the world was fully resident.)
        if (I.unfilled.empty())
            return 0;
        // Nearest-first order around the camera section.
        const int ccx = static_cast<int>(std::floor(cameraPos.x / 16.f));
        const int ccy = static_cast<int>(std::floor(cameraPos.y / 16.f));
        const int ccz = static_cast<int>(std::floor(cameraPos.z / 16.f));
        struct Cand {
            float d;
            i64 key;
        };
        std::vector<Cand> cands;
        cands.reserve(I.unfilled.size());
        for (const i64 key : I.unfilled) {
            const int sx = static_cast<int>((key >> 42) & 0x1FFFFF) - 32768;
            const int sy = static_cast<int>((key >> 21) & 0x1FFFFF) - 32768;
            const int sz = static_cast<int>(key & 0x1FFFFF) - 32768;
            const float dx = static_cast<float>(sx - ccx), dy = static_cast<float>(sy - ccy),
                        dz = static_cast<float>(sz - ccz);
            cands.push_back({dx * dx + dy * dy + dz * dz, key});
        }
        if (cands.empty())
            return 0;
        const size_t take = std::min<size_t>(cands.size(), 32);
        if (take < cands.size())
            std::nth_element(cands.begin(), cands.begin() + take, cands.end(),
                             [](const Cand &a, const Cand &b) { return a.d < b.d; });
        std::sort(cands.begin(), cands.begin() + take,
                  [](const Cand &a, const Cand &b) { return a.d < b.d; });
        const int budget = std::min<int>(I.desc.fillBudgetPerUpdate, static_cast<int>(take));
        // Batch staging: ONE staging buffer + ONE one-shot submit for the
        // whole frame's fills (per-brick one-shots cost a fence wait +
        // submit + wait each: ~70ms/frame at budget 32).
        thread_local std::vector<u16> batchMats;
        thread_local std::vector<u32> batchOcc;
        thread_local std::vector<i64> filledKeys;
        batchMats.resize(static_cast<size_t>(budget) * 4096);
        batchOcc.resize(static_cast<size_t>(budget) * 128);
        std::vector<u32> batchIdx;
        batchIdx.reserve(static_cast<size_t>(budget));
        std::vector<u8> batchOpaque;
        batchOpaque.reserve(static_cast<size_t>(budget));
        filledKeys.clear();
        for (int i = 0; i < budget; ++i) {
            const i64 key = cands[static_cast<size_t>(i)].key;
            const int sx = static_cast<int>((key >> 42) & 0x1FFFFF) - 32768;
            const int sy = static_cast<int>((key >> 21) & 0x1FFFFF) - 32768;
            const int sz = static_cast<int>(key & 0x1FFFFF) - 32768;
            auto sit = I.slots.find(key);
            if (sit == I.slots.end() || sit->second.filled)
                continue;
            u16 *mats = batchMats.data() + static_cast<size_t>(batchIdx.size()) * 4096;
            u32 *occ = batchOcc.data() + static_cast<size_t>(batchIdx.size()) * 128;
            std::fill(occ, occ + 128, 0u);
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
                        if (!skip)
                            occ[vi >> 5u] |= (1u << (vi & 31u));
                        // Fully-opaque brick tracking (hidden-brick culling):
                        // any air/skip/cutout/non-opaque voxel disqualifies.
                        if (skip || (flags & kMcFlagOpaque) == 0u)
                            allOpaque = false;
                    }
                }
            }
            batchIdx.push_back(static_cast<u32>(sit->second.brickIdx));
            batchOpaque.push_back(allOpaque ? 1u : 0u);
            sit->second.filled = true;
            filledKeys.push_back(key);
        }
        if (!batchIdx.empty()) {
            world.UploadBrickBatch(batchIdx.data(), batchMats.data(), batchOcc.data(),
                                   static_cast<u32>(batchIdx.size()));
            for (size_t k = 0; k < batchIdx.size(); ++k) {
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
        return static_cast<int>(I.unfilled.size());
    }

    bool CVoxelMcWorld::HasAnvil() const { return m_Impl->anvil != nullptr; }
} // namespace Manro
