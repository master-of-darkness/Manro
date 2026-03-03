#include <Core/Logger.h>
#include "Sponza.h"

int main(int argc, char **argv) {
    Sponza app;
    app.Initialize();
    app.Run();
    return 0;
}
