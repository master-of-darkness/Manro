#pragma once

#include <format>
#include <functional>
#include <string_view>

namespace Manro {
    enum class LogLevel { Trace, Info, Warn, Error, Critical };

    using LogCallback = std::function<void(LogLevel, std::string_view)>;

    class CLogger {
    public:
        static void Init();

        static void Log(LogLevel level, std::string_view msg);

        static void SetCallback(LogCallback cb);
    };
} // namespace Manro

#define LOG_TRACE(...)    ::Manro::CLogger::Log(::Manro::LogLevel::Trace,    std::format(__VA_ARGS__))
#define LOG_INFO(...)     ::Manro::CLogger::Log(::Manro::LogLevel::Info,     std::format(__VA_ARGS__))
#define LOG_WARN(...)     ::Manro::CLogger::Log(::Manro::LogLevel::Warn,     std::format(__VA_ARGS__))
#define LOG_ERROR(...)    ::Manro::CLogger::Log(::Manro::LogLevel::Error,    std::format(__VA_ARGS__))
#define LOG_CRITICAL(...) ::Manro::CLogger::Log(::Manro::LogLevel::Critical, std::format(__VA_ARGS__))
