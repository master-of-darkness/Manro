#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/Material/MaterialData.h>
#include "../Vulkan/Buffer.h"

#include <vector>
#include <unordered_map>
#include <volk.h>

namespace Manro {
    class VulkanContext;
    class MaterialInstance;

    class MaterialSystem {
    public:
        explicit MaterialSystem(VulkanContext &ctx);

        void Init();

        void Shutdown();

        u32 ResolveMaterialIndex(MaterialInstance &material);

        void FlushToGpu();

        [[nodiscard]] VkBuffer GetMaterialBufferHandle() const { return m_MaterialBuffer->GetHandle(); }

        [[nodiscard]] VkBuffer GetTextureInfoBufferHandle() const { return m_TextureInfoBuffer->GetHandle(); }

        [[nodiscard]] u32 GetMaterialCount() const { return static_cast<u32>(m_Materials.size()); }

        [[nodiscard]] bool IsDirty() const { return m_MaterialsDirty; }

    private:
        VulkanContext &m_Context;

        std::vector<MaterialData> m_Materials;
        std::unordered_map<MaterialData, u32, MaterialDataHash> m_MaterialCache;
        Scope<Buffer> m_MaterialBuffer;
        Scope<Buffer> m_TextureInfoBuffer;
        bool m_MaterialsDirty = true;
    };
} // namespace Manro