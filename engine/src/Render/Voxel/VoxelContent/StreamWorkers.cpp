#include "StreamImpl.h"

#include <Manro/Core/CpuAffinity.h>

#include <thread>

namespace Manro {
    void CVoxelStreamWorld::Impl::ParseLoop(size_t wi) {
        const CpuTopology_t topo = QueryCpuTopology();
        if (topo.IsHybrid() && !topo.m_EfficiencyCores.empty())
            SetThreadAffinity(topo.m_EfficiencyCores);
        std::unique_ptr<IVoxelContentSource> localContent;
        if (content) {
            localContent = content->Clone();
            if (localContent)
                localContent->Open(desc.worldDir);
        }
        std::vector<u32> palLocal;
        VoxelSectionStates saveSec{};
        WorkerState &st = *workerStates[wi];
        for (;;) {
            ParseJob job;
            if (!st.jobs.Pop(job)) {
                std::unique_lock<std::mutex> lk(sleepMutex);
                queueCv.wait(lk, [&] {
                    return stopWorker.load(std::memory_order_acquire) ||
                           !st.jobs.Empty();
                });
                if (stopWorker.load(std::memory_order_acquire))
                    return;
                continue;
            }
            ParseDone out;
            out.key = job.key;
            out.brickIdx = job.brickIdx;
            out.sx = job.sx;
            out.sy = job.sy;
            out.sz = job.sz;
            out.mats.assign(4096, 0);
            out.occ.assign(128, 0u);
            out.fluid.assign(128, 0u);
            bool allOpaque = true;
            bool hasEmis = false;
            palLocal.clear();
            bool haveData = false;
            if (localContent && localContent->IsOpen() &&
                localContent->ReadSectionStates(job.sx, job.sy, job.sz, saveSec) &&
                saveSec.present) {
                haveData = true;
                palLocal.reserve(saveSec.palette.size());
                for (const VoxelBlockState &fs: saveSec.palette) {
                    const auto it =
                            pack->stateByKey.find(MakeStateKey(fs.name, fs.props));
                    palLocal.push_back(it != pack->stateByKey.end()
                                           ? it->second
                                           : pack->fallbackState);
                }
            }
            for (int vi = 0; vi < 4096; ++vi) {
                const u32 st = haveData ? palLocal[saveSec.pal[vi]] : 0u;
                const bool valid = static_cast<size_t>(st) < pack->states.size();
                const u32 flags = valid ? pack->states[static_cast<size_t>(st)].flags : 0u;
                const bool skip = !valid || ((flags & kBlockFlagSkip) != 0);
                out.mats[static_cast<size_t>(vi)] = valid ? static_cast<u16>(st) : 0;
                if (valid && static_cast<size_t>(st) < pack->emission.size() &&
                    pack->emission[static_cast<size_t>(st)] != 0u)
                    hasEmis = true;
                if (!skip) {
                    const bool fluid = static_cast<size_t>(st) < pack->fluid.size() &&
                                       pack->fluid[static_cast<size_t>(st)] != 0u;
                    if (fluid)
                        out.fluid[vi >> 5u] |= (1u << (vi & 31u));
                    else
                        out.occ[vi >> 5u] |= (1u << (vi & 31u));
                }
                if (skip || (flags & kBlockFlagOpaque) == 0u)
                    allOpaque = false;
            }
            out.opaque = allOpaque;
            out.hasEmission = hasEmis;
            while (!st.parsed.Push(std::move(out))) {
                if (stopWorker.load(std::memory_order_acquire))
                    return;
                std::this_thread::yield();
            }
            queueCv.notify_all();
        }
    }

    void CVoxelStreamWorld::Impl::LightLoop(size_t wi) {
        const CpuTopology_t topo = QueryCpuTopology();
        if (topo.IsHybrid() && !topo.m_EfficiencyCores.empty())
            SetThreadAffinity(topo.m_EfficiencyCores);
        std::unique_ptr<IVoxelContentSource> localContent;
        if (content) {
            localContent = content->Clone();
            if (localContent)
                localContent->Open(desc.worldDir);
        }
        WorkerState &st = *workerStates[wi];
        for (;;) {
            ParseDone job;
            if (!st.parsed.Pop(job)) {
                std::unique_lock<std::mutex> lk(sleepMutex);
                queueCv.wait(lk, [&] {
                    return stopWorker.load(std::memory_order_acquire) ||
                           !st.parsed.Empty();
                });
                if (stopWorker.load(std::memory_order_acquire))
                    return;
                continue;
            }
            job.lightVisits = SeedSpreadLight(job.sx, job.sy, job.sz, job.mats,
                                              job.light, localContent.get());
            while (!st.done.Push(std::move(job))) {
                if (stopWorker.load(std::memory_order_acquire))
                    return;
                std::this_thread::yield();
            }
        }
    }

    CVoxelStreamWorld::Impl::~Impl() {
        stopWorker.store(true, std::memory_order_release);
        queueCv.notify_all();
        for (auto &t: workers) {
            if (t.joinable())
                t.join();
        }
        for (auto &t: lightWorkers) {
            if (t.joinable())
                t.join();
        }
    }
}
