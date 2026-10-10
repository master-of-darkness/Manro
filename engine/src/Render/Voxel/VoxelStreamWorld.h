#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/VoxelStreamStats.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Manro {
    class CVoxelWorld;
    struct BlockAssetPack_t;

    struct VoxelStreamDesc_t {

        std::string worldDir;

        int radiusSections{6};

        int fillBudgetPerUpdate{6};
    };

    class CVoxelStreamWorld {
    public:
        CVoxelStreamWorld();
        ~CVoxelStreamWorld();

        CVoxelStreamWorld(const CVoxelStreamWorld &) = delete;
        CVoxelStreamWorld &operator=(const CVoxelStreamWorld &) = delete;

        Vec3 Init(CVoxelWorld &world, const BlockAssetPack_t &pack, const VoxelStreamDesc_t &desc);

        int Update(CVoxelWorld &world, const Vec3 &cameraPos, u32 flightSlot);

        [[nodiscard]] bool IsSolidAt(i64 x, i64 y, i64 z) const;
        [[nodiscard]] bool IsFluidAt(i64 x, i64 y, i64 z) const;
        void ApplyEdit(CVoxelWorld &world, const Vec3 &pos, float radius, u32 op, u32 state = 0u);

        [[nodiscard]] i32 GetStateAt(i64 x, i64 y, i64 z) const;

        [[nodiscard]] bool GetCollisionBoxAt(i64 x, i64 y, i64 z, Vec3 &mn,
                                             Vec3 &mx) const;
        [[nodiscard]] bool BrickHasContent(u32 brickIdx) const;
        [[nodiscard]] i32 PlaceState() const;

        [[nodiscard]] int UnfilledCount() const;

        [[nodiscard]] const VoxelStreamStats_t &GetLastStats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
