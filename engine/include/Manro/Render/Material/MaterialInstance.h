#pragma once

#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <Manro/Render/Material/Material.h>
#include <Manro/Render/Material/MaterialData.h>

namespace Manro {
    class CMaterialInstance {
    public:
        CMaterialInstance(Ref<CMaterial> material)
            : m_Material(material), m_Data(shaderio::defaultGltfMaterial()) {
        }

        void SetTexture(TextureHandle texture) {
            // nvshaders convention: slot 0 = no texture, slot N + 1 = handle index N
            m_Data.pbrBaseColorTexture = (!texture.IsValid())
                                             ? static_cast<uint16_t>(0)
                                             : static_cast<uint16_t>(texture.Index() + 1);
            m_bDirty = true;
        }

        TextureHandle GetTexture() const {
            return (m_Data.pbrBaseColorTexture == 0)
                       ? kInvalidTexture
                       : TextureHandle::Make(m_Data.pbrBaseColorTexture - 1, 0);
        }

        const CMaterial &GetMaterial() const { return *m_Material; }

        Ref<CMaterial> GetMaterialRef() const { return m_Material; }

        u32 GetRendererIndex() const { return m_unRendererIndex; }

        void SetRendererIndex(u32 index) {
            m_unRendererIndex = index;
            m_bDirty = false;
        }

        bool IsDirty() const { return m_bDirty; }

        void MarkDirty() { m_bDirty = true; }

        const MaterialData &GetData() const { return m_Data; }

        MaterialData &ModifyData() {
            m_bDirty = true;
            return m_Data;
        }

    private:
        Ref<CMaterial> m_Material;
        MaterialData m_Data;
        u32 m_unRendererIndex{0xFFFFFFFF};
        bool m_bDirty{true};
    };
} // namespace Manro