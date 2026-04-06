#include "Material.h"
#include "Vulkan/VulkanContext.h"
#include "Vulkan/Pipeline.h"

namespace Manro {
    Material::Material(const VulkanContext &ctx, Scope<Pipeline> pipeline, VkDescriptorSetLayout layout)
        : m_Context(ctx), m_Pipeline(std::move(pipeline)), m_DescriptorSetLayout(layout) {
    }

    Material::~Material() {
        if (m_DescriptorSetLayout) {
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_DescriptorSetLayout, nullptr);
        }
    }

    const Pipeline &Material::GetPipeline() const { return *m_Pipeline; }

    const VulkanContext &Material::GetContext() const { return m_Context; }

    VkPipeline Material::GetHandle() const { return m_Pipeline->GetHandle(); }

    VkPipelineLayout Material::GetLayout() const { return m_Pipeline->GetLayout(); }

    VkDescriptorSetLayout Material::GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }
} // namespace Manro
