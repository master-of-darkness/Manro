#include "EngineContext.h"
#include "Logger.h"

namespace Engine {
    EngineContext::EngineContext() = default;

    EngineContext::~EngineContext() { Shutdown(); }

    void EngineContext::Initialize(bool withPlatform) {
        if (m_IsRunning) return;

        Logger::Init();
        LOG_INFO("[EngineContext] Initializing...");

        m_JobSystem.Initialize();

        if (withPlatform) {
            if (!m_Platform.Initialize()) {
                LOG_ERROR("[EngineContext] Platform initialization failed!");
            }
        }

        m_IsRunning = true;
        LOG_INFO("[EngineContext] Ready.");
    }

    void EngineContext::Shutdown() {
        if (!m_IsRunning) return;

        LOG_INFO("[EngineContext] Shutting down...");
        m_Platform.Shutdown();
        m_JobSystem.Shutdown();
        m_IsRunning = false;
    }
} // namespace Engine