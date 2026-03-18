#pragma once

#include <Manro/Render/Material/Material.h>
#include <Manro/Render/TextureManager.h>
#include <Manro/Core/Types.h>

#include <Manro/Render/Material/MaterialData.h>

namespace Manro {
    class MaterialInstance {
    public:
        MaterialInstance(Ref<Material> material)
            : m_Material(material), m_Data(shaderio::defaultGltfMaterial()) {
        }

        void SetTexture(TextureHandle texture) {
            m_Data.pbrBaseColorTexture = (texture == kInvalidTexture)
                ? static_cast<uint16_t>(0)
                : static_cast<uint16_t>(texture + 1); // nvshaders uses 0 = no texture
        }

        TextureHandle GetTexture() const {
            return (m_Data.pbrBaseColorTexture == 0)
                ? kInvalidTexture
                : static_cast<TextureHandle>(m_Data.pbrBaseColorTexture - 1);
        }

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
