#pragma once

#include <volk.h>
#include <Core/Types.h>

namespace Engine {
    class VulkanContext;
    class ResourceManager;
    struct PBRMaterial;
    struct LightDataGPU;
    class Buffer;

    class DescriptorManager {
    public:
        void Initialize(const VulkanContext& ctx);
        void Shutdown(VkDevice device);

        VkDescriptorPool GetPool() const { return m_DescriptorPool; }
        VkDescriptorSetLayout GetTextureLayout() const { return m_DescriptorSetLayout; }
        VkDescriptorSetLayout GetPBRLightLayout() const { return m_PBRLightSetLayout; }
        VkDescriptorSetLayout GetPBRMaterialLayout() const { return m_PBRMaterialSetLayout; }
        VkSampler GetDefaultSampler() const { return m_Sampler; }

        VkDescriptorSet AllocateSet(VkDevice device, VkDescriptorSetLayout layout);

        void BuildPBRMaterialDescriptor(VkDevice device, PBRMaterial& mat,
                                        const ResourceManager& resources);

        void CreatePBRDescriptorLayouts(VkDevice device);
        void AllocateLightDescriptorSet(VkDevice device, const Buffer& lightUBO);

        VkDescriptorSet GetLightDescriptorSet() const { return m_LightDescriptorSet; }

    private:
        void CreateDescriptorPool(VkDevice device);
        void CreateDescriptorSetLayout(VkDevice device);
        void CreateDefaultSampler(VkDevice device);

        VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_DescriptorSetLayout{VK_NULL_HANDLE};
        VkSampler m_Sampler{VK_NULL_HANDLE};

        VkDescriptorSetLayout m_PBRLightSetLayout{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_PBRMaterialSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet m_LightDescriptorSet{VK_NULL_HANDLE};
    };
} // namespace Engine
