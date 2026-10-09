#include "Voxel.h"
#include <Manro/Core/EngineLoop.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
    void PrintUsage(const char *prog) {
        std::printf("Usage: %s [options]\n"
                    "  --world-dir <path>   Anvil save dir (required, no fallback)\n"
                    "  --assets-dir <path>  <...>/assets/minecraft dir (default: build-time assets)\n"
                    "  --radius <1-10>      section radius (default 6)\n"
                    "  --res-scale <f>      offscreen resolution scale 0.25-1 (default 1)\n"
                    "  --debug              enable GPU debug counters\n"
                    "  --chaos              enable streaming-torture teleports\n"
                    "  --help               print this help\n",
                    prog);
    }

    bool ParseInt(const char *s, int &out) {
        char *end = nullptr;
        const long v = std::strtol(s, &end, 10);
        if (end == s || *end != '\0')
            return false;
        out = static_cast<int>(v);
        return true;
    }

    bool ParseFloat(const char *s, float &out) {
        char *end = nullptr;
        const float v = std::strtof(s, &end);
        if (end == s || *end != '\0')
            return false;
        out = v;
        return true;
    }
}

int main(int argc, char *argv[]) {
    CVoxel::Params params;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char *name, std::string &out) {
            if (i + 1 >= argc) {
                std::printf("Missing value for %s\n", name);
                PrintUsage(argv[0]);
                std::exit(2);
            }
            out = argv[++i];
        };
        if (arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--world-dir") {
            needValue("--world-dir", params.worldDir);
        } else if (arg == "--assets-dir") {
            needValue("--assets-dir", params.assetsDir);
        } else if (arg == "--radius") {
            std::string v;
            needValue("--radius", v);
            int r = 0;
            if (!ParseInt(v.c_str(), r)) {
                std::printf("Invalid --radius '%s'\n", v.c_str());
                return 2;
            }
            params.radius = std::clamp(r, 1, 10);
        } else if (arg == "--res-scale") {
            std::string v;
            needValue("--res-scale", v);
            float f = 0.f;
            if (!ParseFloat(v.c_str(), f)) {
                std::printf("Invalid --res-scale '%s'\n", v.c_str());
                return 2;
            }
            params.resScale = std::clamp(f, 0.25f, 1.f);
        } else if (arg == "--debug") {
            params.debug = true;
        } else if (arg == "--chaos") {
            params.chaos = true;
        } else {
            std::printf("Unknown argument '%s'\n", arg.c_str());
            PrintUsage(argv[0]);
            return 2;
        }
    }
    if (params.worldDir.empty())
        std::printf("[Voxel] warning: no --world-dir given, volume fills as air\n");
    CVoxel app(std::move(params));
    Manro::CEngineLoop::Run(app);
    return 0;
}
