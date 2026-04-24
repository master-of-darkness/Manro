#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Manro::Pack {
    struct PackOptions {
        std::string prefix;
        std::vector<std::filesystem::path> explicitFiles; // empty == walk inputDir
        bool skipHidden = true;
        bool skipEmpty = true;
        std::function<void(const std::string &virtualPath, std::uint64_t bytes)> onFile;
    };

    struct PackResult {
        bool ok = false;
        std::uint32_t entryCount = 0;
        std::uint32_t cdOffset = 0;
        std::string error;
    };

    PackResult PackDirectory(const std::filesystem::path &inputDir,
                             const std::filesystem::path &outputRres,
                             const PackOptions &opts = {});
} // namespace Manro::Pack
