#include "StreamImpl.h"

namespace Manro {
    namespace {
        inline void SeedFlatLight(const BlockAssetPack_t &pack, IVoxelContentSource *content,
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
            u8 topSky[16][16];
            for (int lz = 0; lz < 16; ++lz)
                for (int lx = 0; lx < 16; ++lx)
                    topSky[lx][lz] = 15u;
            if (content && sy < kMaxSectionY) {
                VoxelSectionStates sec{};
                std::vector<u32> palTmp;
                for (int asy = kMaxSectionY; asy > sy; --asy) {
                    if (!content->ReadSectionStates(sx, asy, sz, sec) || !sec.present)
                        continue;
                    palTmp.clear();
                    palTmp.reserve(sec.palette.size());
                    for (const VoxelBlockState &fs: sec.palette) {
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

    bool CVoxelStreamWorld::Impl::PackedLightAt(i64 x, i64 y, i64 z, u8 &skyOut,
                                                u8 &blkOut) const {
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

    void CVoxelStreamWorld::Impl::BorderPullLight(u32 brickIdx, int bsx, int bsy,
                                                  int bsz, u32 faceMask) {
        if (!pack)
            return;
        const size_t base = static_cast<size_t>(brickIdx) * 4096;
        if (base + 4096 > lightMirror.size() || base + 4096 > stateMirror.size())
            return;
        static const int kNb[6][3] = {
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
            {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
        };
        const size_t emisSize = pack->emission.size();
        const size_t skyOpSize = pack->skyOpacity.size();
        for (int ly = 0; ly < 16; ++ly) {
            for (int lz = 0; lz < 16; ++lz) {
                for (int lx = 0; lx < 16; ++lx) {
                    const bool onX = (lx == 15 && (faceMask & (1u << 0u))) ||
                                     (lx == 0 && (faceMask & (1u << 1u)));
                    const bool onY = (ly == 15 && (faceMask & (1u << 2u))) ||
                                     (ly == 0 && (faceMask & (1u << 3u)));
                    const bool onZ = (lz == 15 && (faceMask & (1u << 4u))) ||
                                     (lz == 0 && (faceMask & (1u << 5u)));
                    if (!onX && !onY && !onZ)
                        continue;
                    const size_t vi = static_cast<size_t>(lx + ly * 16 + lz * 256);
                    const u32 st = stateMirror[base + vi];
                    const u8 sop = (static_cast<size_t>(st) < skyOpSize)
                                       ? pack->skyOpacity[static_cast<size_t>(st)]
                                       : 15u;
                    if (sop >= 15u)
                        continue;
                    const int gx = bsx * 16 + lx;
                    const int gy = bsy * 16 + ly;
                    const int gz = bsz * 16 + lz;
                    u8 bestSky = static_cast<u8>(
                        (lightMirror[base + vi] >> 4u) & 0xFu);
                    u8 bestBlk = static_cast<u8>(lightMirror[base + vi] & 0xFu);
                    for (const auto &o: kNb) {
                        u8 ns = 0u, nb = 0u;
                        if (!PackedLightAt(gx + o[0], gy + o[1], gz + o[2], ns, nb))
                            continue;
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

    bool CVoxelStreamWorld::Impl::HaloLevel(const HaloSnap_t halo[6], int bsx, int bsy,
                                            int bsz, int gx, int gy, int gz, u8 &skyOut,
                                            u8 &blkOut) {
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

    u64 CVoxelStreamWorld::Impl::SeedSpreadLight(int sx, int sy, int sz,
                                                 const std::vector<u16> &mats,
                                                 std::vector<u8> &lightOut,
                                                 IVoxelContentSource *content) const {
        SeedFlatLight(*pack, content, sx, sy, sz, mats, lightOut);
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
            return 0;
        HaloSnap_t halo[6];
        SnapshotHalo(sx, sy, sz, halo);
        const size_t skyOpSize = pack->skyOpacity.size();
        const size_t stateSize = pack->states.size();
        const auto skyOpOf = [&](u32 st) -> u8 {
            return (static_cast<size_t>(st) < skyOpSize)
                       ? pack->skyOpacity[static_cast<size_t>(st)]
                       : 15u;
        };
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
                    for (const auto &o: kHaloDir) {
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
        thread_local std::vector<u16> wSkyQ, wBlkQ;
        wSkyQ.clear();
        wBlkQ.clear();
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
            for (const auto &o: kHaloDir) {
                const int nlx = lx + o[0];
                const int nly = ly + o[1];
                const int nlz = lz + o[2];
                if (nlx < 0 || nlx > 15 || nly < 0 || nly > 15 || nlz < 0 ||
                    nlz > 15)
                    continue;
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
                continue;
            const int lx = vi & 15;
            const int ly = (vi >> 4) & 15;
            const int lz = (vi >> 8) & 15;
            for (const auto &o: kHaloDir) {
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

    void CVoxelStreamWorld::Impl::GlowPushToNeighbours(
        u32 brickIdx, int bsx, int bsy, int bsz, std::vector<EditRestage_t> &out) {
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
            if (raised)
                out.push_back({Key(nsx, nsy, nsz), nb});
        }
    }
}
