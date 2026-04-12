#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/RenderSettings.h>
#include "RendererTypes.h"

#include <volk.h>
#include <vector>

namespace Manro {
    class VulkanContext;
    class Pipeline;
    class TextureManager;
    class RenderTargetManager;
    class ShadowSystem;
    class SkyboxRenderer;
    class GpuCullDispatcher;
    class MaterialSystem;
    class Material;

    class PipelineManager {
    public:
        explicit PipelineManager(VulkanContext &ctx);

        void CreateDescriptorLayouts();

        void CreateDescriptorPool(u32 frameCount);

        void Shutdown();

        void BuildPbrPipeline(const RenderTargetManager &rt,
                              const TextureManager &tex,
                              const RenderSettings &settings);

        void BuildCompositePipeline(VkFormat swapchainFormat);

        void UpdatePbrDescriptorSet(u32 fi, FrameData &frame,
                                    const MaterialSystem &matSys,
                                    TextureManager &tex,
                                    const ShadowSystem &shadow,
                                    SkyboxRenderer &skybox);

        void UpdateCompositeDescriptorSet(u32 fi, FrameData &frame,
                                          const RenderTargetManager &rt);

        void UpdateSkyboxDescriptorSet(u32 fi, FrameData &frame,
                                       SkyboxRenderer &skybox,
                                       TextureManager &tex);

        void AllocateFrameDescriptorSets(FrameData &frame,
                                         const GpuCullDispatcher &cull,
                                         const ShadowSystem &shadow,
                                         const SkyboxRenderer &skybox);

        [[nodiscard]] VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
        [[nodiscard]] VkDescriptorSetLayout GetPbrSetLayout() const { return m_PbrSetLayout; }
        [[nodiscard]] VkDescriptorSetLayout GetCompositeSetLayout() const { return m_CompositeSetLayout; }

        [[nodiscard]] Pipeline *GetPbrPipeline() const { return m_PbrPipeline.get(); }
        [[nodiscard]] Pipeline *GetZPrepassPipeline() const { return m_ZPrepassPipeline.get(); }
        [[nodiscard]] Pipeline *GetCompositePipeline() const { return m_CompositePipeline.get(); }

        [[nodiscard]] Ref<Material> GetDefaultMaterial() const { return m_DefaultMaterial; }

    private:
        VulkanContext &m_Context;

        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_PbrSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CompositeSetLayout = VK_NULL_HANDLE;

        Scope<Pipeline> m_PbrPipeline;
        Scope<Pipeline> m_ZPrepassPipeline;
        Scope<Pipeline> m_CompositePipeline;

        Ref<Material> m_DefaultMaterial;
    };
} // namespace Manro