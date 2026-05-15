#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/RenderSettings.h>
#include "../Internal/ShaderTypes.h"
#include "../Internal/FrameData.h"

#include <volk.h>
#include <vector>

namespace Manro {
    class CVulkanContext;
    class CPipeline;
    class CTextureManager;
    class CRenderTargetManager;
    class CShadowSystem;
    class CSkyboxRenderer;
    class CGpuCullDispatcher;
    class CMaterialSystem;
    class CMaterial;
    class CVirtualFS;

    class CPipelineManager {
    public:
        CPipelineManager(CVulkanContext &ctx, CVirtualFS &vfs);

        void CreateDescriptorLayouts();

        void CreateDescriptorPool(u32 frameCount);

        void Shutdown();

        void BuildPbrPipeline(const CRenderTargetManager &rt,
                              const CTextureManager &tex,
                              const RenderSettings_t &settings);

        void BuildCompositePipeline(VkFormat swapchainFormat);

        void UpdatePbrDescriptorSet(u32 fi, const FrameData_t &frame,
                                    const CMaterialSystem &matSys,
                                    const CTextureManager &tex,
                                    const CShadowSystem &shadow,
                                    const CSkyboxRenderer &skybox) const;

        void UpdateCompositeDescriptorSet(u32 fi, const FrameData_t &frame,
                                          const CRenderTargetManager &rt,
                                          VkBuffer autoExposureLuminanceBuffer) const;

        static void UpdateSkyboxDescriptorSet(u32 fi, const FrameData_t &frame,
                                              const CSkyboxRenderer &skybox,
                                              const CTextureManager &tex);

        void AllocateFrameDescriptorSets(FrameData_t &frame,
                                         const CGpuCullDispatcher &cull,
                                         const CShadowSystem &shadow,
                                         const CSkyboxRenderer &skybox) const;

        [[nodiscard]] VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
        [[nodiscard]] VkDescriptorSetLayout GetPbrSetLayout() const { return m_PbrSetLayout; }
        [[nodiscard]] VkDescriptorSetLayout GetCompositeSetLayout() const { return m_CompositeSetLayout; }

        [[nodiscard]] CPipeline *GetPbrPipeline() const { return m_PbrPipeline.get(); }
        [[nodiscard]] CPipeline *GetZPrepassPipeline() const { return m_ZPrepassPipeline.get(); }
        [[nodiscard]] CPipeline *GetCompositePipeline() const { return m_CompositePipeline.get(); }

        [[nodiscard]] Ref<CMaterial> GetDefaultMaterial() const { return m_DefaultMaterial; }

    private:
        CVulkanContext &m_Context;
        CVirtualFS &m_Vfs;

        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_PbrSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CompositeSetLayout = VK_NULL_HANDLE;

        Scope<CPipeline> m_PbrPipeline;
        Scope<CPipeline> m_ZPrepassPipeline;
        Scope<CPipeline> m_CompositePipeline;

        Ref<CMaterial> m_DefaultMaterial;
    };
} // namespace Manro
