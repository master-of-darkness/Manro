#include <Manro/Core/EngineContext.h>
#include <Manro/Core/Logger.h>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

namespace Manro {
    EngineContext::EngineContext(bool withPlatform) {
        Logger::Init();
        LOG_INFO("[EngineContext] Initializing...");

#ifdef _WIN32
        timeBeginPeriod(1);
#endif

        if (withPlatform) {
            m_Platform.emplace();
        }

        LOG_INFO("[EngineContext] Ready.");
    }

    EngineContext::~EngineContext() {
        LOG_INFO("[EngineContext] Shutting down...");
        m_Platform.reset();

#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }
} // namespace Manro
