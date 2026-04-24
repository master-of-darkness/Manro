#include "Editor.h"
#include <Manro/Core/EngineLoop.h>

int main() {
    ManroEdit::CEditor app;
    Manro::CEngineLoop::Run(app);
    return 0;
}
