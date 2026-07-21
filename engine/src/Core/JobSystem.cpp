#include <Manro/Core/JobSystem.h>
#include <Manro/Core/CpuAffinity.h>
#include <Manro/Core/Logger.h>
#include <algorithm>

#include "Profiling.h"

namespace Manro {
    CJobSystem::CJobSystem(u32 numThreads, bool pinToPerformanceCores) {
        CpuTopology_t topo;
        if (pinToPerformanceCores) {
            topo = QueryCpuTopology();
        }

        if (numThreads == 0) {
            if (pinToPerformanceCores) {
                // Reserve one performance core for the main thread
                numThreads = std::max(1u, static_cast<u32>(topo.m_PerformanceCores.size()) - 1u);
            } else {
                numThreads = std::max(1u, std::thread::hardware_concurrency() - 1u);
            }
        }

        m_GlobalHandle = CreateHandle();
        m_Running.store(true, std::memory_order_release);

        for (u32 i = 0; i < numThreads; ++i) {
            u32 pinnedCpu = ~0u;
            if (pinToPerformanceCores && !topo.m_PerformanceCores.empty()) {
                pinnedCpu = topo.m_PerformanceCores[i % topo.m_PerformanceCores.size()];
            }
            m_Threads.emplace_back(&CJobSystem::WorkerThread, this, pinnedCpu);
        }
    }

    CJobSystem::~CJobSystem() {
        {
            std::scoped_lock lock(m_Mutex);
            m_Running.store(false, std::memory_order_release);
        }
        m_WakeCondition.notify_all();

        for (auto &thread: m_Threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        m_Threads.clear();
    }

    CJobHandle CJobSystem::CreateHandle() {
        return CJobHandle(std::make_shared<std::atomic<u32> >(0));
    }

    void CJobSystem::Execute(std::function<void()> job) {
        Execute(m_GlobalHandle, std::move(job));
    }

    void CJobSystem::Execute(CJobHandle handle, std::function<void()> job) {
        if (!m_Running.load(std::memory_order_acquire)) return;
        if (!handle.IsValid()) {
            handle = m_GlobalHandle;
        }

        handle.m_PendingJobs->fetch_add(1, std::memory_order_relaxed);

        {
            std::scoped_lock lock(m_Mutex);
            m_Jobs.push(JobEntry_t{std::move(job), handle.m_PendingJobs});
        }
        m_WakeCondition.notify_one();
    }

    void CJobSystem::Dispatch(u32 jobCount, const std::function<void(u32)> &job) {
        Dispatch(m_GlobalHandle, jobCount, job);
    }

    void CJobSystem::Dispatch(CJobHandle handle, u32 jobCount, const std::function<void(u32)> &job) {
        if (jobCount == 0) return;

        for (u32 i = 0; i < jobCount; ++i) {
            Execute(handle, [job, i]() { job(i); });
        }
    }

    void CJobSystem::Wait(const CJobHandle &handle) {
        if (!handle.IsValid()) return;

        std::unique_lock<std::mutex> lock(m_Mutex);
        m_WakeMain.wait(lock, [&handle]() {
            return handle.m_PendingJobs->load(std::memory_order_acquire) == 0;
        });
    }

    void CJobSystem::WaitAll() {
        Wait(m_GlobalHandle);
    }

    void CJobSystem::WorkerThread(u32 unPinnedCpu) {
        MNR_PROFILE_THREAD("Worker");
        if (unPinnedCpu != ~0u) {
            if (!PinThreadToCpu(unPinnedCpu)) {
                LOG_WARN("[CJobSystem] Failed to pin worker to CPU {}", unPinnedCpu);
            }
        }
        for (;;) {
            JobEntry_t job;

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_WakeCondition.wait(lock, [this]() {
                    return !m_Jobs.empty() || !m_Running.load(std::memory_order_acquire);
                });

                if (m_Jobs.empty()) {
                    return;
                }

                job = std::move(m_Jobs.front());
                m_Jobs.pop();
            }

            if (job.work) {
                MNR_PROFILE_SCOPE("Job");
                job.work();
            }

            {
                std::scoped_lock lock(m_Mutex);
                if (job.pendingJobs) {
                    job.pendingJobs->fetch_sub(1, std::memory_order_acq_rel);
                }
            }
            m_WakeMain.notify_all();
        }
    }
} // namespace Manro
