#include <Manro/Core/VirtualFS.h>
#include <Manro/Core/Logger.h>
#include <fstream>
#include <filesystem>

namespace Manro {
    std::span<const u8> CVirtualFS::Blob_t::Bytes() const {
        if (shared) return std::span<const u8>(shared->data(), shared->size());
        if (viewData) return std::span<const u8>(viewData, viewSize);
        return std::span<const u8>(owned.data(), owned.size());
    }

    void CVirtualFS::MountStaticView(std::string_view virtualPath, std::span<const u8> data) {
        std::unique_lock lock(m_Mutex);
        Blob_t blob;
        blob.viewData = data.data();
        blob.viewSize = data.size();
        m_Blobs[std::string(virtualPath)] = std::move(blob);
    }

    void CVirtualFS::MountOwned(std::string_view virtualPath, std::vector<u8> &&data) {
        std::unique_lock lock(m_Mutex);
        Blob_t blob;
        blob.owned = std::move(data);
        m_Blobs[std::string(virtualPath)] = std::move(blob);
    }

    void CVirtualFS::MountShared(std::string_view virtualPath,
                                 std::shared_ptr<const std::vector<u8>> data) {
        if (!data) {
            LOG_ERROR("[CVirtualFS] MountShared called with null data for '{}'", virtualPath);
            return;
        }
        std::unique_lock lock(m_Mutex);
        Blob_t blob;
        blob.shared = std::move(data);
        m_Blobs[std::string(virtualPath)] = std::move(blob);
    }

    void CVirtualFS::Unmount(std::string_view virtualPath) {
        std::unique_lock lock(m_Mutex);
        m_Blobs.erase(std::string(virtualPath));
    }

    void CVirtualFS::SetBaseDir(std::string_view dir) {
        std::unique_lock lock(m_Mutex);
        m_BaseDir = std::string(dir);
        while (!m_BaseDir.empty() &&
               (m_BaseDir.back() == '/' || m_BaseDir.back() == '\\')) {
            m_BaseDir.pop_back();
        }
        LOG_INFO("[CVirtualFS] Base dir set to: {}", m_BaseDir);
    }

    std::string CVirtualFS::GetBaseDir() const {
        std::shared_lock lock(m_Mutex);
        return m_BaseDir;
    }

    std::string CVirtualFS::ResolvePath(std::string_view path) const {
        if (!path.empty() && (path[0] == '/' || (path.size() > 1 && path[1] == ':'))) {
            return std::string(path);
        }
        std::shared_lock lock(m_Mutex);
        if (m_BaseDir.empty()) {
            return std::string(path);
        }
        return m_BaseDir + "/" + std::string(path);
    }

    std::vector<u8> CVirtualFS::ReadFile(std::string_view path) const {
        {
            std::shared_lock lock(m_Mutex);
            auto it = m_Blobs.find(std::string(path));
            if (it != m_Blobs.end()) {
                auto bytes = it->second.Bytes();
                return std::vector<u8>(bytes.begin(), bytes.end());
            }
        }

        std::string resolved = ResolvePath(path);
        std::ifstream file(resolved, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("[CVirtualFS] Failed to open file: {}", resolved);
            return {};
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<u8> buffer(fileSize);
        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(fileSize));
        return buffer;
    }

    bool CVirtualFS::GetFileSize(std::string_view path, size_t &size) const {
        {
            std::shared_lock lock(m_Mutex);
            auto it = m_Blobs.find(std::string(path));
            if (it != m_Blobs.end()) {
                size = it->second.Bytes().size();
                return true;
            }
        }

        std::string resolved = ResolvePath(path);
        std::error_code ec;
        if (std::filesystem::exists(resolved, ec) && !ec) {
            size = std::filesystem::file_size(resolved, ec);
            return !ec;
        }
        return false;
    }

    bool CVirtualFS::FileExists(std::string_view path) const {
        {
            std::shared_lock lock(m_Mutex);
            if (m_Blobs.find(std::string(path)) != m_Blobs.end()) {
                return true;
            }
        }

        std::string resolved = ResolvePath(path);
        std::error_code ec;
        return std::filesystem::exists(resolved, ec) && !ec
               && !std::filesystem::is_directory(resolved, ec);
    }
} // namespace Manro
