#pragma once

namespace Manro {
    class IApplication;

    class CEngineLoop {
    public:
        static void Run(IApplication &app);
    };
} // namespace Manro