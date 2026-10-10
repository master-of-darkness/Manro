#include "Editor.h"
#include <Manro/Core/EngineLoop.h>

#include <cstdio>
#include <exception>

int main() {
    ManroEdit::CEditor app;
    try {
        Manro::CEngineLoop::Run(app);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[manro-edit] fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
