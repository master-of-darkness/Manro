#pragma once

#include <format>
#include <string_view>

namespace Manro {
    enum class LogLevel { Trace, Info, Warn, Error, Critical };

    class Logger {
    public:
        static void Init();

        static void Log(LogLevel level, std::string_view msg);
    };
} // namespace Manro

#define LOG_TRACE(...)    ::Manro::Logger::Log(::Manro::LogLevel::Trace,    std::format(__VA_ARGS__))
#define LOG_INFO(...)     ::Manro::Logger::Log(::Manro::LogLevel::Info,     std::format(__VA_ARGS__))
#define LOG_WARN(...)     ::Manro::Logger::Log(::Manro::LogLevel::Warn,     std::format(__VA_ARGS__))
#define LOG_ERROR(...)    ::Manro::Logger::Log(::Manro::LogLevel::Error,    std::format(__VA_ARGS__))
#define LOG_CRITICAL(...) ::Manro::Logger::Log(::Manro::LogLevel::Critical, std::format(__VA_ARGS__))
