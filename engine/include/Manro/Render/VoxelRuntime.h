#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/VoxelStreamStats.h>
#include <Manro/Resource/VoxelContentSource.h>

#include <memory>
#include <string>

namespace Manro {
    class CVulkanContext;
    class CVirtualFS;
    class CPipelineCache;
    class CVoxelEncoder;
    struct VoxelFrameDesc;

    class CVoxelRuntime {
    public:
        CVoxelRuntime(CVulkanContext &ctx, CVirtualFS &vfs, CPipelineCache &cache);

        ~CVoxelRuntime();

        CVoxelRuntime(const CVoxelRuntime &) = delete;

        CVoxelRuntime &operator=(const CVoxelRuntime &) = delete;

        void Init(u32 width, u32 height);

        void Shutdown();

        [[nodiscard]] bool IsInitialized() const;

        void AllocateBrick(u32 bx, u32 by, u32 bz);

        void UploadBrick(u32 brickIdx, const u16 *mats, const u32 *occupancy);

        void QueueEdit(const Vec3 &pos, float radius, u32 material, u32 op);

        [[nodiscard]] u32 GetBrickCount() const;

        [[nodiscard]] u32 GetTaskGroups() const;

        void GetTaskSplit(u32 &opaque, u32 &cutout, u32 &skipped) const;

        void GetGpuTimes(float &xferMs, float &drawMs, float &postMs) const;

        void GetGpuInstantTimes(float &xferMs, float &drawMs, float &postMs) const;

        void ReadDebugCounters(u32 out[8]) const;

        void SetDebugEnabled(bool enabled);

        void SetBackfaceEnabled(bool enabled);

        void SetFrustumEnabled(bool enabled);

        Vec3 StreamInit(std::unique_ptr<IVoxelContentSource> content,
                        const std::string &worldDir, const std::string &assetsDir,
                        int radiusSections);

        int StreamUpdate(const Vec3 &cameraPos, u32 flightSlot);

        [[nodiscard]] int GetUnfilledCount() const;

        [[nodiscard]] const VoxelStreamStats_t &GetStreamStats() const;

        [[nodiscard]] bool StreamIsSolid(const Vec3 &p) const;

        [[nodiscard]] bool StreamIsFluid(const Vec3 &p) const;

        [[nodiscard]] u32 StreamPlaceState() const;

        [[nodiscard]] i32 StreamGetStateAt(const Vec3 &p) const;

        [[nodiscard]] bool StreamGetCollisionBox(const Vec3 &p, Vec3 &mn, Vec3 &mx) const;

        [[nodiscard]] std::string GetStateKey(u32 id) const;

        [[nodiscard]] u32 FindStateByKey(const std::string &key) const;

        void Record(CVoxelEncoder &enc, const VoxelFrameDesc &frame);

        void StampPost(CVoxelEncoder &enc, u32 flightSlot, u32 subIdx);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
