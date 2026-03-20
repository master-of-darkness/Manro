#include <Manro/Render/Vulkan/PipelineCache.h>
#include <Manro/Core/Logger.h>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace Manro {
    void PipelineCache::Init(VkDevice device, const std::string &diskPath) {
        m_Device = device;
        m_DiskPath = diskPath;

        VkPipelineCacheCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

        std::vector<u8> blob;
        if (!diskPath.empty()) {
            std::ifstream f(diskPath, std::ios::binary | std::ios::ate);
            if (f.is_open()) {
                blob.resize(static_cast<size_t>(f.tellg()));
                f.seekg(0);
                f.read(reinterpret_cast<char *>(blob.data()),
                       static_cast<std::streamsize>(blob.size()));
                ci.initialDataSize = blob.size();
                ci.pInitialData = blob.data();
                LOG_INFO("[PipelineCache] Loaded {} bytes from '{}'", blob.size(), diskPath);
            }
        }

        if (vkCreatePipelineCache(m_Device, &ci, nullptr, &m_Cache) != VK_SUCCESS)
            throw std::runtime_error("[PipelineCache] vkCreatePipelineCache failed");
    }

    void PipelineCache::Shutdown() {
        m_Graphics.clear();
        m_Compute.clear();

        SaveToDisk();

        if (m_Cache) {
            vkDestroyPipelineCache(m_Device, m_Cache, nullptr);
            m_Cache = VK_NULL_HANDLE;
        }
    }

    VkPipeline PipelineCache::GetGraphics(const PipelineKey &key, GraphicsBuildFn buildFn) {
        auto it = m_Graphics.find(key);
        if (it != m_Graphics.end()) return it->second;

        VkPipeline pso = buildFn(m_Cache);
        if (pso != VK_NULL_HANDLE)
            m_Graphics[key] = pso;
        return pso;
    }

    VkPipeline PipelineCache::GetCompute(const PipelineKey &key, ComputeBuildFn buildFn) {
        auto it = m_Compute.find(key);
        if (it != m_Compute.end()) return it->second;

        VkPipeline pso = buildFn(m_Cache);
        if (pso != VK_NULL_HANDLE)
            m_Compute[key] = pso;
        return pso;
    }

    void PipelineCache::Invalidate(const PipelineKey &key) {
        auto git = m_Graphics.find(key);
        if (git != m_Graphics.end()) {
            vkDestroyPipeline(m_Device, git->second, nullptr);
            m_Graphics.erase(git);
        }
        auto cit = m_Compute.find(key);
        if (cit != m_Compute.end()) {
            vkDestroyPipeline(m_Device, cit->second, nullptr);
            m_Compute.erase(cit);
        }
    }

    void PipelineCache::InvalidateAll() {
        for (auto &[k, pso]: m_Graphics) vkDestroyPipeline(m_Device, pso, nullptr);
        for (auto &[k, pso]: m_Compute) vkDestroyPipeline(m_Device, pso, nullptr);
        m_Graphics.clear();
        m_Compute.clear();
    }

    u64 PipelineCache::HashSpirV(const std::vector<u8> &spv) {
        return HashSpirV(reinterpret_cast<const u32 *>(spv.data()), spv.size() / 4);
    }

    u64 PipelineCache::HashSpirV(const u32 *data, size_t wordCount) {
        u64 hash = 14695981039346656037ull;
        const u8 *bytes = reinterpret_cast<const u8 *>(data);
        for (size_t i = 0; i < wordCount * 4; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    u64 PipelineCache::HashLayouts(const VkDescriptorSetLayout *layouts, u32 count) {
        u64 hash = 14695981039346656037ull;
        for (u32 i = 0; i < count; ++i) {
            u64 v = reinterpret_cast<u64>(layouts[i]);
            const u8 *bytes = reinterpret_cast<const u8 *>(&v);
            for (int b = 0; b < 8; ++b) {
                hash ^= bytes[b];
                hash *= 1099511628211ull;
            }
        }
        return hash;
    }

    void PipelineCache::SaveToDisk() const {
        if (m_DiskPath.empty() || !m_Cache) return;

        size_t dataSize = 0;
        vkGetPipelineCacheData(m_Device, m_Cache, &dataSize, nullptr);
        if (dataSize == 0) return;

        std::vector<u8> blob(dataSize);
        vkGetPipelineCacheData(m_Device, m_Cache, &dataSize, blob.data());

        std::ofstream f(m_DiskPath, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            LOG_WARN("[PipelineCache] Could not write cache to '{}'", m_DiskPath);
            return;
        }
        f.write(reinterpret_cast<const char *>(blob.data()),
                static_cast<std::streamsize>(dataSize));
        LOG_INFO("[PipelineCache] Saved {} bytes to '{}'", dataSize, m_DiskPath);
    }
} // namespace Manro
