#pragma once

#include "Types.h"
#include "JobSystem.h"
#include <Platform/PlatformContext.h>

namespace Engine {
    class EngineContext {
    public:
        EngineContext();

        ~EngineContext();

        EngineContext(const EngineContext &) = delete;

        EngineContext &operator=(const EngineContext &) = delete;

        void Initialize(bool withPlatform = true);

        void Shutdown();

        JobSystem &GetJobSystem() { return m_JobSystem; }
        PlatformContext &GetPlatform() { return m_Platform; }

    private:
        JobSystem m_JobSystem;
        PlatformContext m_Platform;
        bool m_IsRunning{false};
    };
} // namespace Engine