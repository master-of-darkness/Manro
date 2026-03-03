#pragma once

#include <spdlog/spdlog.h>
#include <memory>

namespace Engine {
    class Logger {
    public:
        static void Init();

        inline static std::shared_ptr<spdlog::logger> &GetCoreLogger() { return s_CoreLogger; }

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
    };
} // namespace Engine

#define LOG_TRACE(...)    ::Engine::Logger::GetCoreLogger()->trace(__VA_ARGS__)
#define LOG_INFO(...)     ::Engine::Logger::GetCoreLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)     ::Engine::Logger::GetCoreLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)    ::Engine::Logger::GetCoreLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::Engine::Logger::GetCoreLogger()->critical(__VA_ARGS__)