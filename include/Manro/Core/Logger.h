#pragma once

#include <spdlog/spdlog.h>
#include <memory>

namespace Manro {
    class Logger {
    public:
        static void Init();

        inline static std::shared_ptr<spdlog::logger> &GetCoreLogger() { return s_CoreLogger; }

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
    };
} // namespace Manro

#define LOG_TRACE(...)    ::Manro::Logger::GetCoreLogger()->trace(__VA_ARGS__)
#define LOG_INFO(...)     ::Manro::Logger::GetCoreLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)     ::Manro::Logger::GetCoreLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)    ::Manro::Logger::GetCoreLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::Manro::Logger::GetCoreLogger()->critical(__VA_ARGS__)
