#pragma once

#include "VoxelStreamWorld.h"
#include "VoxelBlocks/VoxelBlockAssets.h"

#include <Manro/Resource/VoxelContentSource.h>
#include "VoxelStorage/VoxelBrickStore.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Manro {
    inline constexpr int kMinSectionY = -4;
    inline constexpr int kMaxSectionY = 19;

    struct CVoxelStreamWorld::Impl {
        template<typename T, size_t kCap>
        struct SpscQueue {
            static_assert((kCap & (kCap - 1)) == 0, "capacity must be a power of two");
            std::array<T, kCap> m_Buf{};
            alignas(64) std::atomic<size_t> m_Head{0};
            alignas(64) std::atomic<size_t> m_Tail{0};

            bool Push(T &&v) {
                const size_t tail = m_Tail.load(std::memory_order_relaxed);
                if (tail - m_Head.load(std::memory_order_acquire) >= kCap)
                    return false;
                m_Buf[tail & (kCap - 1)] = std::move(v);
                m_Tail.store(tail + 1, std::memory_order_release);
                return true;
            }

            bool Pop(T &out) {
                const size_t head = m_Head.load(std::memory_order_relaxed);
                if (m_Tail.load(std::memory_order_acquire) == head)
                    return false;
                out = std::move(m_Buf[head & (kCap - 1)]);
                m_Head.store(head + 1, std::memory_order_release);
                return true;
            }

            [[nodiscard]] bool Empty() const {
                return m_Tail.load(std::memory_order_acquire) ==
                       m_Head.load(std::memory_order_acquire);
            }

            [[nodiscard]] size_t Size() const {
                return m_Tail.load(std::memory_order_acquire) -
                       m_Head.load(std::memory_order_acquire);
            }
        };

        VoxelStreamDesc_t desc{};
        const BlockAssetPack_t *pack{nullptr};
        std::unique_ptr<IVoxelContentSource> content;
        bool hasSave{false};

        struct SlotInfo {
            i32 brickIdx{-1};
            bool filled{false};
        };

        std::unordered_map<i64, SlotInfo> slots;
        std::vector<i32> slotMirror;

        std::vector<i64> unfilled;
        std::vector<std::pair<float, i64> > unfilledScratch;

        struct ParseJob {
            i64 key{-1};
            int sx{0}, sy{0}, sz{0};
            u32 brickIdx{~0u};
        };

        struct ParseDone {
            i64 key{-1};
            u32 brickIdx{~0u};
            std::vector<u16> mats;
            std::vector<u32> occ;
            std::vector<u32> fluid;
            std::vector<u8> light;
            bool opaque{false};
            bool hasEmission{false};
            u64 lightVisits{0};
        };

        struct WorkerState {
            SpscQueue<ParseJob, 64> jobs;
            SpscQueue<ParseDone, 64> done;
        };

        std::vector<std::unique_ptr<WorkerState> > workerStates;
        std::vector<std::thread> workers;
        std::mutex sleepMutex;
        std::condition_variable queueCv;
        std::unordered_set<i64> pending;
        std::atomic<bool> stopWorker{false};
        size_t maxPending{64};

        struct DeferredUpload_t {
            i64 key{-1};
            u32 brickIdx{~0u};
            u8 opaque{0};
            u8 hasEmis{0};
            std::vector<u16> mats;
            std::vector<u32> occ;
            std::vector<u32> fluid;
            std::vector<u8> light;
        };

        std::vector<DeferredUpload_t> deferQueue;
        static constexpr size_t kMaxDeferred = 4096;

        VoxelStreamStats_t lastStats{};
        u64 totalBlockingUploads{0};
        u64 totalDeferredDropped{0};

        void ParseLoop(size_t wi);

        ~Impl();

        int centerCx{0}, centerCz{0};
        bool hasCenter{false};
        int evictRadius{7};
        std::vector<i64> evictPending;
        static constexpr int kEvictPerUpdate = 64;

        int minBX{0}, minBZ{0};

        std::vector<u32> occMirror;
        std::vector<u32> fluidMirror;

        std::vector<u16> stateMirror;
        std::vector<u8> brickContent;
        std::vector<u8> lightMirror;
        std::vector<u8> lightDirtyFlag;
        std::vector<u32> lightDirtyList;
        mutable std::shared_mutex m_MirrorMutex;
        double lightMsAccum{0.0};
        u32 lightBrickCount{0};
        u64 lightVisitCount{0};
        size_t lightQueuePeak{0};
        u64 updateCount{0};
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

        u32 SlotFor(int sx, int sy, int sz) const {
            constexpr u32 M = 64u - 1u;
            const u32 bx = static_cast<u32>(sx - minBX) & M;
            const u32 by = static_cast<u32>(sy + 4) & M;
            const u32 bz = static_cast<u32>(sz - minBZ) & M;
            return bx + by * 64u + bz * 64u * 64u;
        }

        static Vec3 OriginFor(int sx, int sy, int sz) {
            return Vec3(static_cast<float>(sx * 16), static_cast<float>(sy * 16),
                        static_cast<float>(sz * 16));
        }

        bool IsSolidAt(i64 x, i64 y, i64 z) const;

        bool IsFluidAt(i64 x, i64 y, i64 z) const;

        void RecomputeOpaque(CVoxelBrickStore &world, i32 brickIdx);

        bool PackedLightAt(i64 x, i64 y, i64 z, u8 &skyOut, u8 &blkOut) const;

        void BorderPullLight(u32 brickIdx, int bsx, int bsy, int bsz);

        static constexpr int kHaloDir[6][3] = {
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
            {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
        };

        struct HaloSnap_t {
            bool loaded{false};
            u8 light[4096]{};
        };

        void SnapshotHalo(int bsx, int bsy, int bsz, HaloSnap_t out[6]) const;

        static bool HaloLevel(const HaloSnap_t halo[6], int bsx, int bsy, int bsz,
                              int gx, int gy, int gz, u8 &skyOut, u8 &blkOut);

        u64 SeedSpreadLight(int sx, int sy, int sz, const std::vector<u16> &mats,
                            std::vector<u8> &lightOut,
                            IVoxelContentSource *content) const;

        void GlowPushToNeighbours(u32 brickIdx, int bsx, int bsy, int bsz);

        struct EditRestage_t {
            i64 key{-1};
            u32 brick{~0u};
        };

        std::vector<EditRestage_t> editRestage;

        i32 StateAt(i64 x, i64 y, i64 z) const;

        bool CollisionBoxAt(i64 x, i64 y, i64 z, Vec3 &mn, Vec3 &mx) const;
    };
}
