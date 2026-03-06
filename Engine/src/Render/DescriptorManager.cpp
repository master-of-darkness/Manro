#include "DescriptorManager.h"
#include "VulkanContext.h"
#include "ResourceManager.h"
#include "Renderer.h"
#include "Buffer.h"
#include <Core/Logger.h>
#include <stdexcept>

namespace Engine {
    void DescriptorManager::Initialize(const VulkanContext& ctx) {
        VkDevice device = ctx.GetDevice();
        CreateDescriptorPool(device);
        CreateDescriptorSetLayout(device);
        CreateDefaultSampler(device);
    }

    void DescriptorManager::Shutdown(VkDevice device) {
        if (m_PBRLightSetLayout) {
            vkDestroyDescriptorSetLayout(device, m_PBRLightSetLayout, nullptr);
            m_PBRLightSetLayout = VK_NULL_HANDLE;
        }
        if (m_PBRMaterialSetLayout) {
            vkDestroyDescriptorSetLayout(device, m_PBRMaterialSetLayout, nullptr);
            m_PBRMaterialSetLayout = VK_NULL_HANDLE;
        }
        if (m_Sampler) {
            vkDestroySampler(device, m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }
        if (m_DescriptorSetLayout) {
            vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayout, nullptr);
            m_DescriptorSetLayout = VK_NULL_HANDLE;
        }
        if (m_DescriptorPool) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
    }

    void DescriptorManager::CreateDescriptorPool(VkDevice device) {
        VkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = 4096;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[1].descriptorCount = 16;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = 2048;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("[DescriptorManager] Failed to create descriptor pool!");
    }

    void DescriptorManager::CreateDescriptorSetLayout(VkDevice device) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
            throw std::runtime_error("[DescriptorManager] Failed to create descriptor set layout!");
    }

    void DescriptorManager::CreateDefaultSampler(VkDevice device) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

        if (vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
            throw std::runtime_error("[DescriptorManager] Failed to create texture sampler!");
    }

    VkDescriptorSet DescriptorManager::AllocateSet(VkDevice device, VkDescriptorSetLayout layout) {
        VkDescriptorSet set{VK_NULL_HANDLE};

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        vkAllocateDescriptorSets(device, &allocInfo, &set);
        return set;
    }

    void DescriptorManager::CreatePBRDescriptorLayouts(VkDevice device) {
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            if (vkCreateDescriptorSetLayout(device, &layoutInfo,
                                            nullptr, &m_PBRLightSetLayout) != VK_SUCCESS)
                throw std::runtime_error("[DescriptorManager] Failed to create PBR light set layout!");
        }

        {
            VkDescriptorSetLayoutBinding bindings[3]{};
            for (int i = 0; i < 3; ++i) {
                bindings[i].binding = i;
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            }

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 3;
            layoutInfo.pBindings = bindings;

            if (vkCreateDescriptorSetLayout(device, &layoutInfo,
                                            nullptr, &m_PBRMaterialSetLayout) != VK_SUCCESS)
                throw std::runtime_error("[DescriptorManager] Failed to create PBR material set layout!");
        }
    }

    void DescriptorManager::AllocateLightDescriptorSet(VkDevice device, const Buffer& lightUBO) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_PBRLightSetLayout;
        vkAllocateDescriptorSets(device, &allocInfo, &m_LightDescriptorSet);

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = lightUBO.GetHandle();
        bufInfo.offset = 0;
        bufInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_LightDescriptorSet;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void DescriptorManager::BuildPBRMaterialDescriptor(VkDevice device, PBRMaterial& mat,
                                                        const ResourceManager& resources) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_PBRMaterialSetLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo,
                                     &mat.descriptorSet) != VK_SUCCESS) {
            LOG_ERROR("[DescriptorManager] Failed to allocate PBR material descriptor set!");
            mat.descriptorSet = VK_NULL_HANDLE;
            return;
        }

        auto getView = [&](u32 texId, u32 fallback) -> VkImageView {
            const auto* tex = resources.GetTexture(texId ? texId : fallback);
            if (!tex) tex = resources.GetTexture(fallback);
            return tex ? tex->view : VK_NULL_HANDLE;
        };

        VkDescriptorImageInfo imgInfos[3]{};
        imgInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfos[0].imageView = getView(mat.albedoTextureId, resources.GetWhiteTextureId());
        imgInfos[0].sampler = m_Sampler;

        imgInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfos[1].imageView = getView(mat.normalTextureId, resources.GetNeutralNormalTextureId());
        imgInfos[1].sampler = m_Sampler;

        imgInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfos[2].imageView = getView(mat.specularTextureId, resources.GetWhiteTextureId());
        imgInfos[2].sampler = m_Sampler;

        VkWriteDescriptorSet writes[3]{};
        for (int i = 0; i < 3; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = mat.descriptorSet;
            writes[i].dstBinding = i;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &imgInfos[i];
        }
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    }
} // namespace Engine
