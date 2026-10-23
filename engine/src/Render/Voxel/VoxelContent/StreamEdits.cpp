#include "StreamImpl.h"

#include <cmath>

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
                    static const int kNb[6][3] = {
                        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
                    };
                    for (const auto &o: kNb) {
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
        {
            const Impl::EditRestage_t e{
                Impl::Key(bsx, bsy, bsz),
                static_cast<u32>(it->second.brickIdx)
            };
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
}
