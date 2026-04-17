#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>

namespace Manro {
    class CVulkanContext;

    class CPipeline;

    class CMaterial {
    public:
        CMaterial(const CVulkanContext &ctx, Scope<CPipeline> pipeline, VkDescriptorSetLayout layout);

        ~CMaterial();

        CMaterial(const CMaterial &) = delete;

        CMaterial &operator=(const CMaterial &) = delete;

        const CPipeline &GetPipeline() const;

        const CVulkanContext &GetContext() const;

        VkPipeline GetHandle() const;

        VkPipelineLayout GetLayout() const;

        VkDescriptorSetLayout GetDescriptorSetLayout() const;

    private:
        const CVulkanContext &m_Context;
        Scope<CPipeline> m_Pipeline;
        VkDescriptorSetLayout m_DescriptorSetLayout{VK_NULL_HANDLE};
    };
} // namespace Manro
