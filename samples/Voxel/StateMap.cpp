#include "StateMap.h"

#include <Manro/Render/VoxelRuntime.h>

#include <cmath>
#include <map>
#include <string>
#include <utility>

namespace {
    void SplitKey(const std::string &key, std::string &nameOut,
                  std::map<std::string, std::string> &propsOut) {
        const size_t bar = key.find('|');
        nameOut = (bar == std::string::npos) ? key : key.substr(0, bar);
        propsOut.clear();
        if (bar == std::string::npos)
            return;
        size_t i = bar + 1;
        while (i < key.size()) {
            const size_t eq = key.find('=', i);
            if (eq == std::string::npos)
                break;
            const size_t sc = key.find(';', eq + 1);
            if (sc == std::string::npos)
                break;
            propsOut[key.substr(i, eq - i)] = key.substr(eq + 1, sc - eq - 1);
            i = sc + 1;
        }
    }

    std::string JoinKey(const std::string &name,
                        const std::map<std::string, std::string> &props) {
        std::string key = name;
        key += '|';
        for (const auto &p: props) {
            key += p.first;
            key += '=';
            key += p.second;
            key += ';';
        }
        return key;
    }
}

namespace VoxelStateMap {
    Manro::u32 Orient(Manro::CVoxelRuntime &runtime, Manro::u32 base, const Manro::Vec3 &fwd,
                      float hitY, int nx, int ny, int nz, bool targetFluid) {
        const std::string baseKey = runtime.GetStateKey(base);
        if (baseKey.empty())
            return base;
        std::string name;
        std::map<std::string, std::string> props;
        SplitKey(baseKey, name, props);
        if (props.empty())
            return base;

        auto it = props.find("facing");
        if (it != props.end()) {
            const float fx = -fwd.x, fz = -fwd.z;
            const std::string want =
                    (std::fabs(fx) > std::fabs(fz))
                        ? (fx > 0.f ? "east" : "west")
                        : (fz > 0.f ? "south" : "north");
            if (it->second != want) {
                auto trial = props;
                trial["facing"] = want;
                const Manro::u32 id = runtime.FindStateByKey(JoinKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        const bool wantTop =
                (ny > 0) ? false : (ny < 0) ? true : (hitY - std::floor(hitY) > 0.5f);
        it = props.find("half");
        if (it != props.end()) {
            const std::string want = wantTop ? "top" : "bottom";
            if (it->second != want) {
                auto trial = props;
                trial["half"] = want;
                const Manro::u32 id = runtime.FindStateByKey(JoinKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        it = props.find("type");
        if (it != props.end() && (it->second == "top" || it->second == "bottom")) {
            const std::string want = wantTop ? "top" : "bottom";
            if (it->second != want) {
                auto trial = props;
                trial["type"] = want;
                const Manro::u32 id = runtime.FindStateByKey(JoinKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        it = props.find("axis");
        if (it != props.end()) {
            const std::string want = (ny != 0) ? "y" : (nx != 0 ? "x" : "z");
            if (it->second != want) {
                auto trial = props;
                trial["axis"] = want;
                const Manro::u32 id = runtime.FindStateByKey(JoinKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        it = props.find("open");
        if (it != props.end() && it->second != "false") {
            auto trial = props;
            trial["open"] = "false";
            const Manro::u32 id = runtime.FindStateByKey(JoinKey(name, trial));
            if (id != ~0u)
                props = std::move(trial);
        }
        it = props.find("waterlogged");
        if (it != props.end()) {
            const std::string want = targetFluid ? "true" : "false";
            if (it->second != want) {
                auto trial = props;
                trial["waterlogged"] = want;
                const Manro::u32 id = runtime.FindStateByKey(JoinKey(name, trial));
                if (id != ~0u)
                    props = std::move(trial);
            }
        }
        const Manro::u32 resolved = runtime.FindStateByKey(JoinKey(name, props));
        return (resolved != ~0u) ? resolved : base;
    }
}
