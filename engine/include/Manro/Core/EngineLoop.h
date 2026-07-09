#pragma once

namespace Manro {
    class IApplication;
    class CWorld;

    class CEngineLoop {
    public:
        static void Run(IApplication &app);
    };
} // namespace Manro
