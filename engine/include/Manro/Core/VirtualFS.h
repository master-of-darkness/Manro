#pragma once

#include <Manro/Core/Types.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace Manro {
    class VirtualFS {
    public:
        static VirtualFS &Get();

        void Mount(std::string_view virtualPath, const u8 *data, size_t size);

        void MountOwned(std::string_view virtualPath, std::vector<u8> &&data);

        void SetBaseDir(std::string_view dir);

        const std::string &GetBaseDir() const { return m_BaseDir; }

        std::vector<u8> ReadFile(std::string_view path) const;

        bool GetFileSize(std::string_view path, size_t &size) const;

        bool FileExists(std::string_view path) const;

        std::string ResolvePath(std::string_view path) const;

    private:
        VirtualFS() = default;

        struct Blob {
            const u8 *data{nullptr};
            size_t size{0};
            std::vector<u8> ownedStorage;
        };

        std::unordered_map<std::string, Blob> m_Blobs;
        std::string m_BaseDir;
        mutable std::mutex m_Mutex;
    };

    void RegisterEmbeddedShaders();
} // namespace Manro