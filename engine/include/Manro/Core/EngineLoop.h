#pragma once

namespace Manro {
    class IApplication;
    class CWorld;

    class CEngineLoop {
    public:
        static void Run(IApplication &app);
        static CWorld *GetWorld() { return s_World; }
    private:
        static CWorld *s_World;
    };
} // namespace Manro
