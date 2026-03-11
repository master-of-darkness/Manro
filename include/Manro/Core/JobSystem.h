#pragma once

#include <Manro/Core/Types.h>
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

namespace Engine {
    class JobSystem {
    public:
        explicit JobSystem(u32 numThreads = 0);

        ~JobSystem();

        JobSystem(const JobSystem &) = delete;

        JobSystem &operator=(const JobSystem &) = delete;

        void Execute(std::function<void()> job);

        void WaitAll();

    private:
        void WorkerThread();

        std::vector<std::thread> m_Threads;
        std::queue<std::function<void()> > m_Jobs;

        std::mutex m_Mutex;
        std::condition_variable m_WakeCondition;
        std::condition_variable m_WakeMain;

        std::atomic<u32> m_JobsInFlight{0};
        std::atomic<bool> m_Running{false};
    };
} // namespace Engine
