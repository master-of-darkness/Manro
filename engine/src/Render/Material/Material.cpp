#include "Material.h"
#include "Vulkan/VulkanContext.h"
#include "Vulkan/Pipeline.h"

namespace Manro {
    CMaterial::CMaterial(const CVulkanContext &ctx, Scope<CPipeline> pipeline, VkDescriptorSetLayout layout)
        : m_Context(ctx), m_Pipeline(std::move(pipeline)), m_DescriptorSetLayout(layout) {
    }

    CMaterial::~CMaterial() {
        if (m_DescriptorSetLayout) {
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_DescriptorSetLayout, nullptr);
        }
    }

    const CPipeline &CMaterial::GetPipeline() const { return *m_Pipeline; }

    const CVulkanContext &CMaterial::GetContext() const { return m_Context; }

    VkPipeline CMaterial::GetHandle() const { return m_Pipeline->GetHandle(); }

    VkPipelineLayout CMaterial::GetLayout() const { return m_Pipeline->GetLayout(); }

    VkDescriptorSetLayout CMaterial::GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }
} // namespace Manro
