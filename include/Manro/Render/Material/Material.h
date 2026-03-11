#pragma once

#include <Manro/Render/Vulkan/Pipeline.h>
#include <Manro/Core/Types.h>
#include <Manro/Render/Vulkan/VulkanContext.h> // Added this include for VulkanContext

namespace Engine {
    class Material {
    public:
        Material(const VulkanContext &ctx, Scope<Pipeline> pipeline, VkDescriptorSetLayout layout)
            : m_Context(ctx), m_Pipeline(std::move(pipeline)), m_DescriptorSetLayout(layout) {
        }

        ~Material() {
            if (m_DescriptorSetLayout) {
                vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_DescriptorSetLayout, nullptr);
            }
        }

        const Pipeline &GetPipeline() const { return *m_Pipeline; }
        const VulkanContext &GetContext() const { return m_Context; }
        VkPipeline GetHandle() const { return m_Pipeline->GetHandle(); }
        VkPipelineLayout GetLayout() const { return m_Pipeline->GetLayout(); }
        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }

    private:
        const VulkanContext &m_Context;
        Scope<Pipeline> m_Pipeline;
        VkDescriptorSetLayout m_DescriptorSetLayout{VK_NULL_HANDLE};
    };
} // namespace Engine
