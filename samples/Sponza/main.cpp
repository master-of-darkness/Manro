#include "Sponza.h"
#include "Manro/Core/EngineLoop.h"

#include <cstdio>
#include <exception>

int main() {
    CSponza app;
    try {
        Manro::CEngineLoop::Run(app);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[Sponza] fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}