#include <Manro/Core/VirtualFS.h>
#include <Manro/Core/Logger.h>
#include <fstream>
#include <filesystem>

namespace Manro {
    CVirtualFS &CVirtualFS::Get() {
        static CVirtualFS instance;
        return instance;
    }

    void CVirtualFS::Mount(std::string_view virtualPath, const u8 *data, size_t size) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Blobs[std::string(virtualPath)] = Blob_t{data, size, {}};
    }

    void CVirtualFS::MountOwned(std::string_view virtualPath, std::vector<u8> &&data) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        size_t size = data.size();
        const u8 *ptr = data.data();
        m_Blobs[std::string(virtualPath)] = Blob_t{ptr, size, std::move(data)};
    }

    void CVirtualFS::SetBaseDir(std::string_view dir) {
        m_BaseDir = std::string(dir);
        while (!m_BaseDir.empty() &&
               (m_BaseDir.back() == '/' || m_BaseDir.back() == '\\')) {
            m_BaseDir.pop_back();
        }
        LOG_INFO("[CVirtualFS] Base dir set to: {}", m_BaseDir);
    }

    std::string CVirtualFS::ResolvePath(std::string_view path) const {
        if (!path.empty() && (path[0] == '/' || (path.size() > 1 && path[1] == ':'))) {
            return std::string(path);
        }
        if (m_BaseDir.empty()) {
            return std::string(path);
        }
        return m_BaseDir + "/" + std::string(path);
    }

    std::vector<u8> CVirtualFS::ReadFile(std::string_view path) const {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_Blobs.find(std::string(path));
            if (it != m_Blobs.end()) {
                const Blob_t &blob = it->second;
                return std::vector<u8>(blob.data, blob.data + blob.size);
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
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_Blobs.find(std::string(path));
            if (it != m_Blobs.end()) {
                size = it->second.size;
                return true;
            }
        }

        std::string resolved = ResolvePath(path);
        if (std::filesystem::exists(resolved)) {
            size = std::filesystem::file_size(resolved);
            return true;
        }
        return false;
    }

    bool CVirtualFS::FileExists(std::string_view path) const {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Blobs.find(std::string(path)) != m_Blobs.end()) {
                return true;
            }
        }

        std::string resolved = ResolvePath(path);
        return std::filesystem::exists(resolved) && !std::filesystem::is_directory(resolved);
    }
} // namespace Manro