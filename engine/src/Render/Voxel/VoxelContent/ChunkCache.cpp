#include "StreamImpl.h"

#include <cstring>

namespace Manro {
    bool CVoxelStreamWorld::Impl::IsSolidAt(i64 x, i64 y, i64 z) const {
        std::shared_lock lk(m_MirrorMutex);
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

    bool CVoxelStreamWorld::Impl::IsFluidAt(i64 x, i64 y, i64 z) const {
        std::shared_lock lk(m_MirrorMutex);
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

    void CVoxelStreamWorld::Impl::SnapshotHalo(int bsx, int bsy, int bsz,
                                               HaloSnap_t out[6]) const {
        std::shared_lock lk(m_MirrorMutex);
        std::shared_lock llk(m_LightMutex);
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

    i32 CVoxelStreamWorld::Impl::StateAt(i64 x, i64 y, i64 z) const {
        std::shared_lock lk(m_MirrorMutex);
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

    bool CVoxelStreamWorld::Impl::CollisionBoxAt(i64 x, i64 y, i64 z, Vec3 &mn,
                                                 Vec3 &mx) const {
        std::shared_lock lk(m_MirrorMutex);
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
}
