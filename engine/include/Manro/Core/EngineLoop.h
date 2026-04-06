#pragma once

namespace Manro {
    class IApplication;

    class EngineLoop {
    public:
        static void Run(IApplication &app);
    };
} // namespace Manro