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
            m_Dirty = true;
        }

        TextureHandle GetTexture() const {
            return (m_Data.pbrBaseColorTexture == 0)
                ? kInvalidTexture
                : static_cast<TextureHandle>(m_Data.pbrBaseColorTexture - 1);
        }

        const Material &GetMaterial() const { return *m_Material; }
        Ref<Material> GetMaterialRef() const { return m_Material; }

        u32 GetRendererIndex() const { return m_RendererIndex; }
        void SetRendererIndex(u32 index) {
            m_RendererIndex = index;
            m_Dirty = false;
        }

        bool IsDirty() const { return m_Dirty; }
        void MarkDirty() { m_Dirty = true; }

        const MaterialData &GetData() const { return m_Data; }

        MaterialData &ModifyData() {
            m_Dirty = true;
            return m_Data;
        }
    private:
        Ref<Material> m_Material;
        MaterialData m_Data;
        u32 m_RendererIndex{0xFFFFFFFF};
        bool m_Dirty{true};
    };
} // namespace Manro
