#include <Manro/Core/Logger.h>
#include "Sponza.h"

int main(int argc, char **argv) {
    Sponza app = Sponza();
    app.Initialize();
    app.Run();
    return 0;
}
