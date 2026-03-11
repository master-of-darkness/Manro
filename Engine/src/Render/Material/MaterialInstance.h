#pragma once

#include "Material.h"
#include <Render/TextureManager.h>
#include <Core/Types.h>

namespace Engine {
    class MaterialInstance {
    public:
        MaterialInstance(Ref<Material> material) : m_Material(material) {}

        void SetTexture(TextureHandle texture) { m_Texture = texture; }
        TextureHandle GetTexture() const { return m_Texture; }

        void SetTintColor(const Vec3& color) { m_TintColor = color; }
        Vec3 GetTintColor() const { return m_TintColor; }

        const Material& GetMaterial() const { return *m_Material; }
        Ref<Material> GetMaterialRef() const { return m_Material; }

    private:
        Ref<Material> m_Material;
        TextureHandle m_Texture{kInvalidTexture};
        Vec3 m_TintColor{1.0f, 1.0f, 1.0f};
    };
} // namespace Engine
