#include <Manro/Resource/RresMount.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Core/Logger.h>

#include "rres.h"

#include <cstring>
#include <mutex>
#include <string>

namespace Manro {
    namespace {
        std::mutex g_MountMutex;
        std::vector<std::string> g_MountedPaths;

        std::string JoinVirtualPath(std::string_view prefix, const char *entry) {
            std::string result;
            result.reserve(prefix.size() + std::strlen(entry) + 1);
            result.append(prefix);
            if (!result.empty() && result.back() != '/' && entry[0] != '/')
                result.push_back('/');
            // Skip leading slash on entry to avoid producing //
            const char *e = entry;
            if (!result.empty() && result.back() == '/' && *e == '/') ++e;
            result.append(e);
            return result;
        }
    } // anon namespace

    bool CRresMount::MountArchive(CVirtualFS &vfs, std::string_view archivePath,
                                  std::string_view virtualPrefix) {
        const std::string pathStr(archivePath);

        // Read the central directory to discover (id, filename) pairs
        rresCentralDir dir = rresLoadCentralDirectory(pathStr.c_str());
        if (dir.count == 0 || dir.entries == nullptr) {
            LOG_ERROR("[CRresMount] '{}' has no central directory; cannot enumerate entries.",
                      pathStr);
            rresUnloadCentralDirectory(dir);
            return false;
        }

        u32 mountedCount = 0;
        for (unsigned int i = 0; i < dir.count; ++i) {
            const rresDirEntry &entry = dir.entries[i];
            rresResourceChunk chunk = rresLoadResourceChunk(pathStr.c_str(), entry.id);

            // Only RAW chunks map directly into VirtualFS
            const unsigned int dataType = rresGetDataType(chunk.info.type);
            if (dataType != RRES_DATA_RAW) {
                const char fourCC[5] = {
                    static_cast<char>(chunk.info.type[0]),
                    static_cast<char>(chunk.info.type[1]),
                    static_cast<char>(chunk.info.type[2]),
                    static_cast<char>(chunk.info.type[3]),
                    '\0'
                };
                LOG_INFO("[CRresMount] Skipping non-RAW chunk '{}' (type {}).",
                         entry.fileName, fourCC);
                rresUnloadResourceChunk(chunk);
                continue;
            }

            if (chunk.info.compType != RRES_COMP_NONE ||
                chunk.info.cipherType != RRES_CIPHER_NONE) {
                LOG_ERROR("[CRresMount] '{}' is compressed/encrypted; not supported in v1.",
                          entry.fileName);
                rresUnloadResourceChunk(chunk);
                continue;
            }

            if (chunk.data.raw == nullptr) {
                LOG_ERROR("[CRresMount] Empty payload for '{}'.", entry.fileName);
                rresUnloadResourceChunk(chunk);
                continue;
            }

            // RAW props[0] = byte size of payload
            const size_t size = (chunk.data.propCount > 0)
                                    ? static_cast<size_t>(chunk.data.props[0])
                                    : 0u;
            if (size == 0) {
                rresUnloadResourceChunk(chunk);
                continue;
            }

            std::vector<u8> bytes(size);
            std::memcpy(bytes.data(), chunk.data.raw, size);

            std::string virtualPath = JoinVirtualPath(virtualPrefix, entry.fileName);
            vfs.MountOwned(virtualPath, std::move(bytes));

            {
                std::lock_guard<std::mutex> lock(g_MountMutex);
                g_MountedPaths.push_back(virtualPath);
            }
            ++mountedCount;

            rresUnloadResourceChunk(chunk);
        }

        rresUnloadCentralDirectory(dir);
        LOG_INFO("[CRresMount] Mounted {} entr{} from '{}'.",
                 mountedCount, mountedCount == 1 ? "y" : "ies", pathStr);
        return mountedCount > 0;
    }

    const std::vector<std::string> &CRresMount::GetMountedPaths() {
        return g_MountedPaths;
    }
} // namespace Manro
