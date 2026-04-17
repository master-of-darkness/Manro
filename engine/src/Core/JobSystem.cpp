#include <Manro/Core/JobSystem.h>
#include <algorithm>

#include "Profiling.h"

namespace Manro {
    CJobSystem::CJobSystem(u32 numThreads) {
        if (numThreads == 0) {
            numThreads = std::max(1u, std::thread::hardware_concurrency() - 1u);
        }

        m_GlobalHandle = CreateHandle();
        m_Running = true;

        for (u32 i = 0; i < numThreads; ++i) {
            m_Threads.emplace_back(&CJobSystem::WorkerThread, this);
        }
    }

    CJobSystem::~CJobSystem() {
        m_Running = false;
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
        if (!m_Running) return;
        if (!handle.IsValid()) {
            handle = m_GlobalHandle;
        }

        {
            std::scoped_lock lock(m_Mutex);
            handle.m_PendingJobs->fetch_add(1, std::memory_order_relaxed);
            m_Jobs.push(JobEntry_t{std::move(job), handle.m_PendingJobs});
            ++m_JobsInFlight;
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

        while (handle.m_PendingJobs->load(std::memory_order_acquire) > 0) {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_WakeMain.wait(lock, [&handle]() {
                return handle.m_PendingJobs->load(std::memory_order_acquire) == 0;
            });
        }
    }

    void CJobSystem::WaitAll() {
        Wait(m_GlobalHandle);
    }

    void CJobSystem::WorkerThread() {
        MNR_PROFILE_THREAD("Worker");
        while (m_Running) {
            JobEntry_t job;

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_WakeCondition.wait(lock, [this]() {
                    return !m_Jobs.empty() || !m_Running;
                });

                if (!m_Running && m_Jobs.empty()) {
                    return;
                }

                job = std::move(m_Jobs.front());
                m_Jobs.pop();
            }

            if (job.work) {
                MNR_PROFILE_SCOPE("Job");
                job.work();
                job.pendingJobs->fetch_sub(1, std::memory_order_release);
                m_JobsInFlight--;
                if (m_JobsInFlight == 0 || job.pendingJobs->load(std::memory_order_acquire) == 0) {
                    m_WakeMain.notify_all();
                }
            }
        }
    }
} // namespace Manro