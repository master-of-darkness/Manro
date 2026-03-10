#pragma once

#include "Types.h"
#include "JobSystem.h"
#include <Platform/PlatformContext.h>
#include <optional>

namespace Engine {
    class EngineContext {
    public:
        explicit EngineContext(bool withPlatform = true);

        ~EngineContext();

        EngineContext(const EngineContext &) = delete;

        EngineContext &operator=(const EngineContext &) = delete;

        JobSystem &GetJobSystem() { return m_JobSystem; }

        bool HasPlatform() const { return m_Platform.has_value(); }
        PlatformContext &GetPlatform() { return m_Platform.value(); }

    private:
        JobSystem m_JobSystem;
        std::optional<PlatformContext> m_Platform;
    };
} // namespace Engine