#pragma once

#include <Manro/Core/Types.h>
#include <string>
#include <string_view>
#include <vector>

namespace Manro {
    // Mounts .rres archive to VFS
    class CRresMount {
    public:
        // Open `archivePath` and mount every RRES_DATA_RAW chunk into CVirtualFS
        static bool MountArchive(std::string_view archivePath,
                                 std::string_view virtualPrefix = {});

        // List virtual paths previously mounted from any archive
        static const std::vector<std::string> &GetMountedPaths();
    };
} // namespace Manro
