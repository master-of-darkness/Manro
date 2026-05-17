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
    class CJobHandle {
    public:
        CJobHandle() = default;

        [[nodiscard]] bool IsValid() const { return static_cast<bool>(m_PendingJobs); }

    private:
        friend class CJobSystem;

        explicit CJobHandle(std::shared_ptr<std::atomic<u32> > pendingJobs)
            : m_PendingJobs(std::move(pendingJobs)) {
        }

        std::shared_ptr<std::atomic<u32> > m_PendingJobs;
    };

    class CJobSystem {
    public:
        explicit CJobSystem(u32 numThreads = 0);

        ~CJobSystem();

        CJobSystem(const CJobSystem &) = delete;

        CJobSystem &operator=(const CJobSystem &) = delete;

        [[nodiscard]] CJobHandle CreateHandle();

        void Execute(std::function<void()> job);

        void Execute(CJobHandle handle, std::function<void()> job);

        void Dispatch(u32 jobCount, const std::function<void(u32)> &job);

        void Dispatch(CJobHandle handle, u32 jobCount, const std::function<void(u32)> &job);

        void Wait(const CJobHandle &handle);

        void WaitAll();

    private:
        struct JobEntry_t {
            std::function<void()> work;
            std::shared_ptr<std::atomic<u32> > pendingJobs;
        };

        void WorkerThread();

        std::vector<std::thread> m_Threads;
        std::queue<JobEntry_t> m_Jobs;

        std::mutex m_Mutex;
        std::condition_variable m_WakeCondition;
        std::condition_variable m_WakeMain;

        std::atomic<bool> m_Running{false};
        CJobHandle m_GlobalHandle;
    };
} // namespace Manro