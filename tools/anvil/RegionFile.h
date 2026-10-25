#pragma once

#include <Manro/Core/Types.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Manro::Anvil {
    bool ReadFile(const std::string &path, std::vector<u8> &out);

    bool InflateAuto(const u8 *src, size_t srcLen, std::vector<u8> &dst);

    class CRegionFile {
    public:
        CRegionFile();

        ~CRegionFile();

        CRegionFile(const CRegionFile &) = delete;

        CRegionFile &operator=(const CRegionFile &) = delete;

        bool Open(const std::string &regionDir);

        [[nodiscard]] bool IsOpen() const;

        bool LoadChunkPayload(int cx, int cz, std::vector<u8> &payload);

        void ClearCache();

        [[nodiscard]] const std::string &GetRegionDir() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
