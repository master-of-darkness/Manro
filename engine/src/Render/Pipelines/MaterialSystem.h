#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/Material/MaterialData.h>
#include "../Vulkan/Buffer.h"

#include <vector>
#include <unordered_map>
#include <volk.h>

namespace Manro {
    class CVulkanContext;
    class CMaterialInstance;

    class CMaterialSystem {
    public:
        explicit CMaterialSystem(CVulkanContext &ctx);

        void Init();

        void Shutdown();

        u32 ResolveMaterialIndex(CMaterialInstance &material);

        void FlushToGpu();

        [[nodiscard]] VkBuffer GetMaterialBufferHandle() const { return m_MaterialBuffer->GetHandle(); }

        [[nodiscard]] VkBuffer GetTextureInfoBufferHandle() const { return m_TextureInfoBuffer->GetHandle(); }

        [[nodiscard]] u32 GetMaterialCount() const { return static_cast<u32>(m_Materials.size()); }

        [[nodiscard]] bool IsDirty() const { return m_bMaterialsDirty; }

    private:
        CVulkanContext &m_Context;

        std::vector<MaterialData> m_Materials;
        std::unordered_map<MaterialData, u32, MaterialDataHash_t> m_MaterialCache;
        Scope<CBuffer> m_MaterialBuffer;
        Scope<CBuffer> m_TextureInfoBuffer;
        bool m_bMaterialsDirty = true;
    };
} // namespace Manro