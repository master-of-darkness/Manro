#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/RenderSettings.h>
#include <Manro/Render/Renderer.h>
#include "../Vulkan/VulkanHelpers.h"
#include "../Core/Profiling.h"
#include <volk.h>

namespace Manro {
    class CVulkanContext;
    class CPipeline;
    class CBuffer;
    class CMeshManager;

    struct ShadowUniformData_t {
        Mat4 lightViewProj;
        Vec4 lightDir;
        Vec2 shadowMapSize;
        float normalBias;
        float softShadows;
        int shadowsEnabled;
        float _pad[3];
    };

    struct ShadowFrameResources_t {
        VkDescriptorSet pbrSet{VK_NULL_HANDLE};
        VkBuffer instanceBuffer{VK_NULL_HANDLE};
    };

    struct ShadowPushConstants_t {
        Mat4 lightViewProj;
    };

    class CShadowSystem {
    public:
        explicit CShadowSystem(CVulkanContext &ctx);

        ~CShadowSystem() = default;

        CShadowSystem(const CShadowSystem &) = delete;

        CShadowSystem &operator=(const CShadowSystem &) = delete;

        void SetGpuProfileCtx(MnrGpuProfileCtx ctx) { m_GpuProfileCtx = ctx; }

        void Init(VkDescriptorPool pool, const ShadowSettings_t &s,
                  VkDescriptorSetLayout pbrSetLayout);

        void Recreate(VkDescriptorPool pool, const ShadowSettings_t &s,
                      VkDescriptorSetLayout pbrSetLayout,
                      std::vector<VkDescriptorSet> &pbrSets);

        void Shutdown();

        void RenderPass(VkCommandBuffer cb,
                        VkDescriptorSet pbrSet,
                        VkBuffer instanceBuffer,
                        u32 totalInstCount,
                        VkBuffer indexBuffer,
                        VkBuffer vertexBuffer,
                        VkBuffer shadowIndirectBuffer,
                        VkBuffer shadowCountBuffer,
                        const std::vector<LightData> &pendingLights,
                        const ShadowSettings_t &s);

        void UpdatePbrDescriptorSetShadow(VkDescriptorSet pbrSet) const;

        static Mat4 ComputeLightViewProj(const Vec3 &lightDir);

        bool IsEnabled() const { return m_bEnabled; }
        VkImageView GetShadowMapView() const { return m_ShadowMap.view; }
        VkSampler GetShadowSampler() const { return m_ShadowSampler; }

        VkPipeline GetPipeline() const;

        VkPipelineLayout GetPipelineLayout() const;

        VkDescriptorSetLayout GetMeshCullSetLayout() const { return m_ShadowMeshCullSetLayout; }
        const ShadowUniformData_t &GetUniform() const { return m_ShadowUniform; }

        VkBuffer GetUniformBufferHandle() const;

    private:
        void CreateResources(const ShadowSettings_t &s);

        void BuildPipeline(VkDescriptorSetLayout pbrSetLayout);

        void BuildMeshCullLayout(VkDescriptorPool pool);

        CVulkanContext &m_Context;

        AllocatedImage_t m_ShadowMap{};
        VkSampler m_ShadowSampler{VK_NULL_HANDLE};
        Scope<CBuffer> m_ShadowUniformBuffer;
        ShadowUniformData_t m_ShadowUniform{};
        bool m_bEnabled{false};

        Scope<CPipeline> m_ShadowPipeline;
        VkDescriptorSetLayout m_ShadowMeshCullSetLayout{VK_NULL_HANDLE};

        MnrGpuProfileCtx m_GpuProfileCtx{};
    };
} // namespace Manro