#include <Manro/Core/Logger.h>
#include <string>

#include "Game.h"

int main(int argc, char **argv) {
    GameMode mode = GameMode::Client;

    if (argc > 1) {
        std::string argStr = argv[1];
        if (argStr == "--server") {
            mode = GameMode::DedicatedServer;
        } else if (argStr == "--host") {
            mode = GameMode::ListenServer;
        }
    }

    Game app(mode);
    app.Initialize();

    app.Run();

    return 0;
}
