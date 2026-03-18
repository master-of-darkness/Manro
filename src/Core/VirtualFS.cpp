#include <Manro/Core/VirtualFS.h>
#include <Manro/Core/Logger.h>
#include <fstream>
#include <filesystem>

namespace Manro {
    VirtualFS &VirtualFS::Get() {
        static VirtualFS instance;
        return instance;
    }

    void VirtualFS::Mount(std::string_view virtualPath, const u8 *data, size_t size) {
        m_Blobs[std::string(virtualPath)] = Blob{data, size};
        LOG_INFO("[VirtualFS] Mounted blob: {} ({} bytes)", virtualPath, size);
    }

    void VirtualFS::SetBaseDir(std::string_view dir) {
        m_BaseDir = std::string(dir);
        while (!m_BaseDir.empty() &&
               (m_BaseDir.back() == '/' || m_BaseDir.back() == '\\')) {
            m_BaseDir.pop_back();
        }
        LOG_INFO("[VirtualFS] Base dir set to: {}", m_BaseDir);
    }

    std::string VirtualFS::ResolvePath(std::string_view path) const {
        if (!path.empty() && (path[0] == '/' || (path.size() > 1 && path[1] == ':'))) {
            return std::string(path);
        }
        if (m_BaseDir.empty()) {
            return std::string(path);
        }
        return m_BaseDir + "/" + std::string(path);
    }

    std::vector<u8> VirtualFS::ReadFile(std::string_view path) const {
        auto it = m_Blobs.find(std::string(path));
        if (it != m_Blobs.end()) {
            const Blob &blob = it->second;
            return std::vector<u8>(blob.data, blob.data + blob.size);
        }

        std::string resolved = ResolvePath(path);
        std::ifstream file(resolved, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("[VirtualFS] Failed to open file: {}", resolved);
            return {};
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<u8> buffer(fileSize);
        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(fileSize));
        return buffer;
    }

    bool VirtualFS::FileExists(std::string_view path) const {
        if (m_Blobs.find(std::string(path)) != m_Blobs.end()) {
            return true;
        }

        std::string resolved = ResolvePath(path);
        return std::filesystem::exists(resolved) && !std::filesystem::is_directory(resolved);
    }
} // namespace Manro