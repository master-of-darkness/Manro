#include <Manro/Core/Logger.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Manro {
    static std::shared_ptr<spdlog::logger> s_CoreLogger;
    static LogCallback s_LogCallback;

    void CLogger::Init() {
        spdlog::set_pattern("%^[%T] %n: %v%$");
        s_CoreLogger = spdlog::stdout_color_mt("Manro");
        s_CoreLogger->set_level(spdlog::level::trace);
    }

    void CLogger::SetCallback(LogCallback cb) {
        s_LogCallback = std::move(cb);
    }

    void CLogger::Log(LogLevel level, std::string_view msg) {
        if (!s_CoreLogger) return;
        switch (level) {
            case LogLevel::Trace: s_CoreLogger->trace(msg);
                break;
            case LogLevel::Info: s_CoreLogger->info(msg);
                break;
            case LogLevel::Warn: s_CoreLogger->warn(msg);
                break;
            case LogLevel::Error: s_CoreLogger->error(msg);
                break;
            case LogLevel::Critical: s_CoreLogger->critical(msg);
                break;
        }
        if (s_LogCallback) s_LogCallback(level, msg);
    }
} // namespace Manro