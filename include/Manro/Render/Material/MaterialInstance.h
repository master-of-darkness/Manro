#pragma once

#include <Manro/Render/Material/Material.h>
#include <Manro/Render/TextureManager.h>
#include <Manro/Core/Types.h>

#include <Manro/Render/Material/MaterialData.h>

namespace Manro {
    class MaterialInstance {
    public:
        MaterialInstance(Ref<Material> material) : m_Material(material) {
        }

        void SetTexture(TextureHandle texture) {
            m_Data.baseColorTexIndex = (int)texture;
            m_Data.baseColorTextureSet = (texture == kInvalidTexture) ? -1 : 0;
        }

        TextureHandle GetTexture() const { return (TextureHandle)m_Data.baseColorTexIndex; }

        const Material &GetMaterial() const { return *m_Material; }
        Ref<Material> GetMaterialRef() const { return m_Material; }

        MaterialData &GetData() { return m_Data; }
        const MaterialData &GetData() const { return m_Data; }

        void CreateDescriptorSets(VkDescriptorPool pool, uint32_t count) {
            // No longer needed for GPU-driven
        }

        VkDescriptorSet GetDescriptorSet(uint32_t frameIndex) const {
            return VK_NULL_HANDLE;
        }

    private:
        Ref<Material> m_Material;
        MaterialData m_Data;
    };
} // namespace Manro
