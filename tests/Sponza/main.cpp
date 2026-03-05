#include <Core/Logger.h>
#include "Sponza.h"

int main(int argc, char **argv) {
    SceneType scene = SceneType::Bistro;

    Sponza app(scene);
    app.Initialize();
    app.Run();
    return 0;
}
