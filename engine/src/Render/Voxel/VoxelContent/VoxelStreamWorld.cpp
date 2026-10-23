#include "StreamImpl.h"

namespace Manro {
    CVoxelStreamWorld::CVoxelStreamWorld() : m_Impl(std::make_unique<Impl>()) {}
    CVoxelStreamWorld::~CVoxelStreamWorld() = default;

    int CVoxelStreamWorld::UnfilledCount() const {
        return static_cast<int>(m_Impl->unfilled.size());
    }

    const VoxelStreamStats_t &CVoxelStreamWorld::GetLastStats() const {
        return m_Impl->lastStats;
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
