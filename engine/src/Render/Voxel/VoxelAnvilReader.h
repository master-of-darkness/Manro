#pragma once

#include <Manro/Core/Types.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Manro {

    struct FullState {
        std::string name;
        std::vector<std::pair<std::string, std::string>> props;
    };

    struct AnvilSectionStates {
        u16 pal[4096]{0};
        std::vector<FullState> palette;
        bool present{false};
    };

    inline std::string MakeStateKey(
        const std::string &name, const std::vector<std::pair<std::string, std::string>> &props) {
        std::string key;
        key.reserve(name.size() + props.size() * 16u);
        key += name;
        key += '|';
        for (const auto &p : props) {
            key += p.first;
            key += '=';
            key += p.second;
            key += ';';
        }
        return key;
    }

    class CAnvilWorldReader {
    public:
        CAnvilWorldReader() = default;
        ~CAnvilWorldReader();

        CAnvilWorldReader(const CAnvilWorldReader &) = delete;
        CAnvilWorldReader &operator=(const CAnvilWorldReader &) = delete;

        bool Open(const std::string &worldDir);

        [[nodiscard]] bool IsOpen() const { return m_bOpen; }

        bool ReadSectionStates(int sx, int sy, int sz, AnvilSectionStates &out);

        bool ScanSaveStates(std::vector<FullState> &uniqueOut);

        bool ReadSpawn(Vec3 &spawn) const;

        [[nodiscard]] const std::string &GetRegionDir() const { return m_RegionDir; }

    private:
        struct Impl;
        Impl *m_Impl{nullptr};
        std::string m_WorldDir;
        std::string m_RegionDir;
        bool m_bOpen{false};
    };
}
