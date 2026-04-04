#pragma once

#include <Manro/Core/Types.h>
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>

namespace Manro {
    class JobHandle {
    public:
        JobHandle() = default;

        bool IsValid() const { return static_cast<bool>(m_PendingJobs); }

    private:
        friend class JobSystem;

        explicit JobHandle(std::shared_ptr<std::atomic<u32> > pendingJobs)
            : m_PendingJobs(std::move(pendingJobs)) {
        }

        std::shared_ptr<std::atomic<u32> > m_PendingJobs;
    };

    class JobSystem {
    public:
        explicit JobSystem(u32 numThreads = 0);

        ~JobSystem();

        JobSystem(const JobSystem &) = delete;

        JobSystem &operator=(const JobSystem &) = delete;

        JobHandle CreateHandle();

        void Execute(std::function<void()> job);

        void Execute(JobHandle handle, std::function<void()> job);

        void Dispatch(u32 jobCount, const std::function<void(u32)> &job);

        void Dispatch(JobHandle handle, u32 jobCount, const std::function<void(u32)> &job);

        void Wait(const JobHandle &handle);

        void WaitAll();

    private:
        struct JobEntry {
            std::function<void()> work;
            std::shared_ptr<std::atomic<u32> > pendingJobs;
        };

        void WorkerThread();

        std::vector<std::thread> m_Threads;
        std::queue<JobEntry> m_Jobs;

        std::mutex m_Mutex;
        std::condition_variable m_WakeCondition;
        std::condition_variable m_WakeMain;

        std::atomic<u32> m_JobsInFlight{0};
        std::atomic<bool> m_Running{false};
        JobHandle m_GlobalHandle;
    };
} // namespace Manro