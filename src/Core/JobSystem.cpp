#include <Manro/Core/JobSystem.h>
#include <algorithm>

namespace Manro {
    JobSystem::JobSystem(u32 numThreads) {
        if (numThreads == 0) {
            numThreads = std::max(1u, std::thread::hardware_concurrency() - 1u);
        }

        m_Running = true;

        for (u32 i = 0; i < numThreads; ++i) {
            m_Threads.emplace_back(&JobSystem::WorkerThread, this);
        }
    }

    JobSystem::~JobSystem() {
        m_Running = false;
        m_WakeCondition.notify_all();

        for (auto &thread: m_Threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        m_Threads.clear();
    }

    void JobSystem::Execute(std::function<void()> job) {
        if (!m_Running) return;

        {
            std::scoped_lock lock(m_Mutex);
            m_Jobs.push(std::move(job));
            m_JobsInFlight++;
        }
        m_WakeCondition.notify_one();
    }

    void JobSystem::WaitAll() {
        while (m_JobsInFlight > 0) {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_WakeMain.wait(lock, [this]() { return m_JobsInFlight == 0; });
        }
    }

    void JobSystem::WorkerThread() {
        while (m_Running) {
            std::function<void()> job;

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

            if (job) {
                job();
                m_JobsInFlight--;
                if (m_JobsInFlight == 0) {
                    m_WakeMain.notify_all();
                }
            }
        }
    }
} // namespace Manro
