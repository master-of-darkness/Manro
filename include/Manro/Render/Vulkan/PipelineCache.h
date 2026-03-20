#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Manro {
    enum PipelineVariant : u32 {
        PipelineVariant_None = 0,
        PipelineVariant_AlphaBlend = 1 << 0, // standard alpha blending
        PipelineVariant_AlphaToCov = 1 << 1, // alpha-to-coverage
        PipelineVariant_DoubleSided = 1 << 2, // VK_CULL_MODE_NONE
        PipelineVariant_Wireframe = 1 << 3, // VK_POLYGON_MODE_LINE
        PipelineVariant_DepthOnly = 1 << 4, // no colour attachment (shadow pass)
        PipelineVariant_DepthPrepass = 1 << 5, // colour writes disabled, depth write on
        PipelineVariant_Compute = 1 << 6, // marks this as a compute PSO key
    };

    struct PipelineKey {
        u64 vertHash = 0; // FNV-1a of the SPIR-V bytecode
        u64 fragHash = 0; // 0 for compute pipelines
        u64 compHash = 0; // 0 for graphics pipelines
        u32 variants = PipelineVariant_None;
        VkFormat colorFmt = VK_FORMAT_UNDEFINED;
        VkFormat depthFmt = VK_FORMAT_UNDEFINED;
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        u32 pushConstantSize = 0;
        u32 setLayoutCount = 0;
        u64 setLayoutHash = 0;

        bool operator==(const PipelineKey &o) const {
            return vertHash == o.vertHash
                   && fragHash == o.fragHash
                   && compHash == o.compHash
                   && variants == o.variants
                   && colorFmt == o.colorFmt
                   && depthFmt == o.depthFmt
                   && msaaSamples == o.msaaSamples
                   && pushConstantSize == o.pushConstantSize
                   && setLayoutCount == o.setLayoutCount
                   && setLayoutHash == o.setLayoutHash;
        }
    };

    struct PipelineKeyHash {
        size_t operator()(const PipelineKey &k) const noexcept {
            const u8 *data = reinterpret_cast<const u8 *>(&k);
            size_t hash = 14695981039346656037ull;
            for (size_t i = 0; i < sizeof(PipelineKey); ++i) {
                hash ^= data[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }
    };

    using GraphicsBuildFn = std::function<VkPipeline(VkPipelineCache)>;
    using ComputeBuildFn = std::function<VkPipeline(VkPipelineCache)>;

    class PipelineCache {
    public:
        void Init(VkDevice device, const std::string &diskPath = "");

        void Shutdown();

        VkPipeline GetGraphics(const PipelineKey &key, GraphicsBuildFn buildFn);

        VkPipeline GetCompute(const PipelineKey &key, ComputeBuildFn buildFn);

        void Invalidate(const PipelineKey &key);

        void InvalidateAll();

        VkPipelineCache VkHandle() const { return m_Cache; }

        static u64 HashSpirV(const std::vector<u8> &spv);

        static u64 HashSpirV(const u32 *data, size_t wordCount);

        static u64 HashLayouts(const VkDescriptorSetLayout *layouts, u32 count);

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        VkPipelineCache m_Cache = VK_NULL_HANDLE;
        std::string m_DiskPath;

        using PSOMap = std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash>;
        PSOMap m_Graphics;
        PSOMap m_Compute;

        void LoadFromDisk();

        void SaveToDisk() const;
    };
} // namespace Manro
