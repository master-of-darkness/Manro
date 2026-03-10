#include "EngineContext.h"
#include "Logger.h"

namespace Engine {
    EngineContext::EngineContext(bool withPlatform) {
        Logger::Init();
        LOG_INFO("[EngineContext] Initializing...");

        if (withPlatform) {
            m_Platform.emplace();
        }

        LOG_INFO("[EngineContext] Ready.");
    }

    EngineContext::~EngineContext() {
        LOG_INFO("[EngineContext] Shutting down...");
        m_Platform.reset();
    }
} // namespace Engine