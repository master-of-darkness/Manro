#pragma once

#include <Manro/Core/Types.h>
#include <memory>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Manro {
    class CVirtualFS {
    public:
        CVirtualFS() = default;

        ~CVirtualFS() = default;

        CVirtualFS(const CVirtualFS &) = delete;

        CVirtualFS &operator=(const CVirtualFS &) = delete;

        void MountStaticView(std::string_view virtualPath, std::span<const u8> data);

        void MountOwned(std::string_view virtualPath, std::vector<u8> &&data);

        void MountShared(std::string_view virtualPath,
                         std::shared_ptr<const std::vector<u8>> data);

        void Unmount(std::string_view virtualPath);

        void SetBaseDir(std::string_view dir);

        std::string GetBaseDir() const;

        std::vector<u8> ReadFile(std::string_view path) const;

        bool GetFileSize(std::string_view path, size_t &size) const;

        bool FileExists(std::string_view path) const;

        std::string ResolvePath(std::string_view path) const;

    private:
        struct Blob_t {
            std::vector<u8> owned;
            std::shared_ptr<const std::vector<u8>> shared;
            const u8 *viewData{nullptr};
            size_t viewSize{0};

            std::span<const u8> Bytes() const;
        };

        std::unordered_map<std::string, Blob_t> m_Blobs;
        std::string m_BaseDir;
        mutable std::shared_mutex m_Mutex;
    };
} // namespace Manro
