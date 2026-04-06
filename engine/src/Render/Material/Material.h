#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>

namespace Manro {
    class VulkanContext;

    class Pipeline;

    class Material {
    public:
        Material(const VulkanContext &ctx, Scope<Pipeline> pipeline, VkDescriptorSetLayout layout);

        ~Material();

        Material(const Material &) = delete;

        Material &operator=(const Material &) = delete;

        const Pipeline &GetPipeline() const;

        const VulkanContext &GetContext() const;

        VkPipeline GetHandle() const;

        VkPipelineLayout GetLayout() const;

        VkDescriptorSetLayout GetDescriptorSetLayout() const;

    private:
        const VulkanContext &m_Context;
        Scope<Pipeline> m_Pipeline;
        VkDescriptorSetLayout m_DescriptorSetLayout{VK_NULL_HANDLE};
    };
} // namespace Manro
