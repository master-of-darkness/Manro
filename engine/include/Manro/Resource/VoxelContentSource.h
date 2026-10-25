#pragma once

#include <Manro/Core/Types.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Manro {
    struct VoxelBlockState {
        std::string name;
        std::vector<std::pair<std::string, std::string> > props;
    };

    struct VoxelSectionStates {
        u16 pal[4096]{0};
        std::vector<VoxelBlockState> palette;
        bool present{false};
    };

    inline std::string MakeStateKey(
        const std::string &name, const std::vector<std::pair<std::string, std::string> > &props) {
        std::string key;
        key.reserve(name.size() + props.size() * 16u);
        key += name;
        key += '|';
        for (const auto &p: props) {
            key += p.first;
            key += '=';
            key += p.second;
            key += ';';
        }
        return key;
    }

    class IVoxelContentSource {
    public:
        virtual ~IVoxelContentSource() = default;

        virtual bool Open(const std::string &worldDir) = 0;

        [[nodiscard]] virtual bool IsOpen() const = 0;

        [[nodiscard]] virtual std::unique_ptr<IVoxelContentSource> Clone() const = 0;

        virtual bool ReadSectionStates(int sx, int sy, int sz, VoxelSectionStates &out) = 0;

        virtual void TrimCaches() {
        }

        virtual bool ScanSaveStates(std::vector<VoxelBlockState> &uniqueOut) = 0;

        [[nodiscard]] virtual bool ReadSpawn(Vec3 &spawn) const = 0;
    };
}
