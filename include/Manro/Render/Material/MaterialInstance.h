#pragma once

#include <Manro/Render/Material/Material.h>
#include <Manro/Render/TextureManager.h>
#include <Manro/Core/Types.h>

namespace Manro {
    class MaterialInstance {
    public:
        MaterialInstance(Ref<Material> material) : m_Material(material) {
        }

        void SetTexture(TextureHandle texture) { m_Texture = texture; }
        TextureHandle GetTexture() const { return m_Texture; }

        const Material &GetMaterial() const { return *m_Material; }
        Ref<Material> GetMaterialRef() const { return m_Material; }

        void CreateDescriptorSets(VkDescriptorPool pool, uint32_t count) {
            m_DescriptorSets.resize(count);
            std::vector<VkDescriptorSetLayout> layouts(count, m_Material->GetDescriptorSetLayout());
            
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = pool;
            allocInfo.descriptorSetCount = count;
            allocInfo.pSetLayouts = layouts.data();
            
            if (vkAllocateDescriptorSets(m_Material->GetContext().GetDevice(), &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate material descriptor sets!");
            }
        }
 
        VkDescriptorSet GetDescriptorSet(uint32_t frameIndex) const {
            return m_DescriptorSets[frameIndex];
        }

    private:
        Ref<Material> m_Material;
        TextureHandle m_Texture{kInvalidTexture};
        std::vector<VkDescriptorSet> m_DescriptorSets;
    };
} // namespace Manro
