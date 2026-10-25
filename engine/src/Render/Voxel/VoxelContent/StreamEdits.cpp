#include "StreamImpl.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Manro {
    void CVoxelStreamWorld::Impl::RecomputeOpaque(CVoxelBrickStore &world, i32 brickIdx) {
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

    void CVoxelStreamWorld::ApplyEdit(CVoxelBrickStore &world, const Vec3 &pos, float radius,
                                      u32 op, u32 state) {
        auto &I = *m_Impl;
        std::unique_lock lk(I.m_MirrorMutex);
        std::unique_lock llk(I.m_LightMutex);

        const float reach = radius < 0.5f ? 0.f : radius + 0.5f;
        const int centerBx = static_cast<int>(std::floor(pos.x / 16.f));
        const int centerBy = static_cast<int>(std::floor(pos.y / 16.f));
        const int centerBz = static_cast<int>(std::floor(pos.z / 16.f));

        int bx0 = centerBx;
        int bx1 = centerBx;
        int by0 = centerBy;
        int by1 = centerBy;
        int bz0 = centerBz;
        int bz1 = centerBz;
        if (radius >= 0.5f) {
            bx0 = static_cast<int>(std::floor((pos.x - reach) / 16.f));
            bx1 = static_cast<int>(std::floor((pos.x + reach) / 16.f));
            by0 = static_cast<int>(std::floor((pos.y - reach) / 16.f));
            by1 = static_cast<int>(std::floor((pos.y + reach) / 16.f));
            bz0 = static_cast<int>(std::floor((pos.z - reach) / 16.f));
            bz1 = static_cast<int>(std::floor((pos.z + reach) / 16.f));
            bx0 = std::max(bx0, centerBx - 2);
            bx1 = std::min(bx1, centerBx + 2);
            by0 = std::max(by0, centerBy - 2);
            by1 = std::min(by1, centerBy + 2);
            bz0 = std::max(bz0, centerBz - 2);
            bz1 = std::min(bz1, centerBz + 2);
        }

        std::vector<u32> editedVi;
        for (int bz = bz0; bz <= bz1; ++bz) {
            for (int by = by0; by <= by1; ++by) {
                for (int bx = bx0; bx <= bx1; ++bx) {
                    const i64 key = Impl::Key(bx, by, bz);
                    const auto it = I.slots.find(key);
                    if (it == I.slots.end() || !it->second.filled ||
                        it->second.brickIdx < 0)
                        continue;
                    const i32 brickIdx = it->second.brickIdx;
                    const size_t bi = static_cast<size_t>(brickIdx);
                    const size_t base = bi * 128;
                    if (base + 128 > I.occMirror.size() ||
                        base + 128 > I.fluidMirror.size())
                        continue;
                    const size_t sbase = bi * 4096;
                    const bool hasState = sbase + 4096 <= I.stateMirror.size();
                    const bool hasLight = sbase + 4096 <= I.lightMirror.size();
                    const bool fluidState =
                            op != 0u && I.pack &&
                            static_cast<size_t>(state) < I.pack->fluid.size() &&
                            I.pack->fluid[static_cast<size_t>(state)] != 0u;

                    editedVi.clear();
                    const auto applyVoxel = [&](int lx, int ly, int lz) {
                        const size_t vi = static_cast<size_t>(lx + ly * 16 + lz * 256);
                        const size_t w = base + (vi >> 5u);
                        const u32 bit = 1u << (vi & 31u);
                        if (op == 0u) {
                            I.occMirror[w] &= ~bit;
                            I.fluidMirror[w] &= ~bit;
                            if (hasState)
                                I.stateMirror[sbase + vi] = 0u;
                        } else if (fluidState) {
                            I.occMirror[w] &= ~bit;
                            I.fluidMirror[w] |= bit;
                            if (hasState)
                                I.stateMirror[sbase + vi] =
                                        static_cast<u16>(state & 0xFFFFu);
                        } else {
                            I.occMirror[w] |= bit;
                            I.fluidMirror[w] &= ~bit;
                            if (hasState)
                                I.stateMirror[sbase + vi] =
                                        static_cast<u16>(state & 0xFFFFu);
                        }
                        editedVi.push_back(static_cast<u32>(vi));
                    };

                    if (radius < 0.5f) {
                        if (bx != centerBx || by != centerBy || bz != centerBz)
                            continue;
                        const int vx = static_cast<int>(std::floor(pos.x));
                        const int vy = static_cast<int>(std::floor(pos.y));
                        const int vz = static_cast<int>(std::floor(pos.z));
                        const int lx = vx - bx * 16;
                        const int ly = vy - by * 16;
                        const int lz = vz - bz * 16;
                        if (lx < 0 || lx > 15 || ly < 0 || ly > 15 ||
                            lz < 0 || lz > 15)
                            continue;
                        applyVoxel(lx, ly, lz);
                    } else {
                        for (int lz = 0; lz < 16; ++lz) {
                            for (int ly = 0; ly < 16; ++ly) {
                                for (int lx = 0; lx < 16; ++lx) {
                                    const float dx = static_cast<float>(bx * 16 + lx) +
                                                     0.5f - pos.x;
                                    const float dy = static_cast<float>(by * 16 + ly) +
                                                     0.5f - pos.y;
                                    const float dz = static_cast<float>(bz * 16 + lz) +
                                                     0.5f - pos.z;
                                    if (dx * dx + dy * dy + dz * dz > reach * reach)
                                        continue;
                                    applyVoxel(lx, ly, lz);
                                }
                            }
                        }
                    }
                    if (editedVi.empty())
                        continue;

                    u32 faceMask = 0u;
                    for (const u32 vi: editedVi) {
                        const int lx = static_cast<int>(vi & 15u);
                        const int ly = static_cast<int>((vi >> 4u) & 15u);
                        const int lz = static_cast<int>((vi >> 8u) & 15u);
                        if (lx == 15) faceMask |= 1u << 0u;
                        if (lx == 0) faceMask |= 1u << 1u;
                        if (ly == 15) faceMask |= 1u << 2u;
                        if (ly == 0) faceMask |= 1u << 3u;
                        if (lz == 15) faceMask |= 1u << 4u;
                        if (lz == 0) faceMask |= 1u << 5u;
                    }

                    I.RecomputeOpaque(world, brickIdx);

                    if (hasLight && I.pack) {
                        for (const u32 vi: editedVi) {
                            const int lx = static_cast<int>(vi & 15u);
                            const int ly = static_cast<int>((vi >> 4u) & 15u);
                            const int lz = static_cast<int>((vi >> 8u) & 15u);
                            const int vx = bx * 16 + lx;
                            const int vy = by * 16 + ly;
                            const int vz = bz * 16 + lz;
                            const u32 stNow = hasState ? I.stateMirror[sbase + vi] : 0u;
                            u8 emis = 0u, sop = 15u;
                            if (static_cast<size_t>(stNow) < I.pack->emission.size())
                                emis = I.pack->emission[static_cast<size_t>(stNow)];
                            if (static_cast<size_t>(stNow) < I.pack->skyOpacity.size())
                                sop = I.pack->skyOpacity[static_cast<size_t>(stNow)];
                            u8 sky = 0u, blk = emis;
                            if (vy >= 320) {
                                sky = 15u;
                            } else if (vy >= -64 && sop < 15u) {
                                static const int kNb[6][3] = {
                                    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                    {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
                                };
                                for (const auto &o: kNb) {
                                    u8 ns = 0u, nb = 0u;
                                    if (!I.PackedLightAt(vx + o[0], vy + o[1],
                                                         vz + o[2], ns, nb))
                                        continue;
                                    u8 skyCand;
                                    if (ns == 15 && o[1] == -1 && sop == 0u)
                                        skyCand = 15u;
                                    else
                                        skyCand = ns > 0u
                                                      ? static_cast<u8>(ns - 1u)
                                                      : 0u;
                                    const u8 blkCand =
                                            nb > 0u ? static_cast<u8>(nb - 1u) : 0u;
                                    if (skyCand > sky)
                                        sky = skyCand;
                                    if (blkCand > blk)
                                        blk = blkCand;
                                }
                            }
                            I.lightMirror[sbase + vi] =
                                    static_cast<u8>((sky << 4u) | (blk & 0xFu));
                        }
                    }

                    u32 any = 0u;
                    for (size_t w = 0; w < 128; ++w)
                        any |= (I.occMirror[base + w] | I.fluidMirror[base + w]);
                    if (I.brickContent.size() <= bi)
                        I.brickContent.resize(bi + 1, 1u);
                    const u8 newContent = (any != 0u) ? 1u : 0u;
                    if (I.brickContent[bi] != newContent) {
                        I.brickContent[bi] = newContent;
                        world.BumpVisibilityVersion();
                    }
                    if (I.streamBrick.size() <= bi)
                        I.streamBrick.resize(bi + 1, 0u);
                    I.streamBrick[bi] = 1u;

                    const Impl::EditRestage_t e{key, static_cast<u32>(brickIdx)};
                    bool dup = false;
                    for (const auto &q: I.editRestage) {
                        if (q.key == e.key) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup)
                        I.editRestage.push_back(e);

                    I.MarkLightDirty(key, static_cast<u32>(brickIdx), faceMask);
                    for (int a = 0; a < 6; ++a) {
                        if ((faceMask & (1u << a)) == 0u)
                            continue;
                        const auto &o = Impl::kHaloDir[a];
                        const i64 nkey =
                                Impl::Key(bx + o[0], by + o[1], bz + o[2]);
                        const auto nit = I.slots.find(nkey);
                        if (nit != I.slots.end() && nit->second.filled &&
                            nit->second.brickIdx >= 0) {
                            I.MarkLightDirty(
                                nkey, static_cast<u32>(nit->second.brickIdx),
                                1u << (a ^ 1));
                        }
                    }
                }
            }
        }
    }
}
