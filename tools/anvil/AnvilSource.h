#pragma once

#include <Manro/Resource/VoxelContentSource.h>

#include <memory>
#include <string>

namespace Manro {
    class CAnvilContentSource final : public IVoxelContentSource {
    public:
        CAnvilContentSource();

        ~CAnvilContentSource() override;

        CAnvilContentSource(const CAnvilContentSource &) = delete;

        CAnvilContentSource &operator=(const CAnvilContentSource &) = delete;

        bool Open(const std::string &worldDir) override;

        [[nodiscard]] bool IsOpen() const override;

        [[nodiscard]] std::unique_ptr<IVoxelContentSource> Clone() const override;

        bool ReadSectionStates(int sx, int sy, int sz, VoxelSectionStates &out) override;

        bool ScanSaveStates(std::vector<VoxelBlockState> &uniqueOut) override;

        [[nodiscard]] bool ReadSpawn(Vec3 &spawn) const override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        std::string m_WorldDir;
        bool m_bOpen{false};
    };
}
