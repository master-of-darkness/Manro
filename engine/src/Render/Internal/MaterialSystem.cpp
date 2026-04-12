#include "MaterialSystem.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Buffer.h"

#include <Manro/Render/Material/MaterialInstance.h>

namespace Manro {
    MaterialSystem::MaterialSystem(VulkanContext &ctx)
        : m_Context(ctx) {
    }

    void MaterialSystem::Init() {
        m_MaterialBuffer = CreateScope<Buffer>(
            m_Context, sizeof(MaterialData) * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_TextureInfoBuffer = CreateScope<Buffer>(
            m_Context, sizeof(shaderio::GltfTextureInfo) * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        std::vector<shaderio::GltfTextureInfo> tis(1024);
        for (int i = 0; i < 1024; ++i) {
            tis[i].uvTransform = Mat3x2(1.f);
            tis[i].index = i - 1;
            tis[i].texCoord = 0;
        }
        m_TextureInfoBuffer->LoadData(tis.data(), sizeof(shaderio::GltfTextureInfo) * 1024);

        m_Materials.push_back(shaderio::defaultGltfMaterial());
        m_MaterialBuffer->LoadData(m_Materials.data(), sizeof(MaterialData));
        m_MaterialsDirty = false;
    }

    void MaterialSystem::Shutdown() {
        m_MaterialBuffer.reset();
        m_TextureInfoBuffer.reset();
        m_Materials.clear();
        m_MaterialCache.clear();
    }

    u32 MaterialSystem::ResolveMaterialIndex(MaterialInstance &material) {
        u32 matIndex = material.GetRendererIndex();
        if (material.IsDirty() || matIndex == 0xFFFFFFFF) {
            const MaterialData &md = material.GetData();
            auto it = m_MaterialCache.find(md);
            if (it != m_MaterialCache.end()) {
                matIndex = it->second;
            } else {
                matIndex = static_cast<u32>(m_Materials.size());
                m_Materials.push_back(md);
                m_MaterialCache[md] = matIndex;
                m_MaterialsDirty = true;
            }
            material.SetRendererIndex(matIndex);
        }
        return matIndex;
    }

    void MaterialSystem::FlushToGpu() {
        if (m_MaterialsDirty && !m_Materials.empty()) {
            m_MaterialBuffer->LoadData(m_Materials.data(), sizeof(MaterialData) * m_Materials.size());
            m_MaterialsDirty = false;
        }
    }
} // namespace Manro