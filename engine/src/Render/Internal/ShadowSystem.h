#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Profiling.h>
#include <Manro/Render/RenderSettings.h>
#include <Manro/Render/Renderer.h>
#include "../Vulkan/VulkanHelpers.h"
#include <volk.h>

namespace Manro {
    class VulkanContext;
    class Pipeline;
    class Buffer;
    class MeshManager;

    struct ShadowUniformData {
        Mat4 lightViewProj;
        Vec4 lightDir;
        Vec2 shadowMapSize;
        float normalBias;
        float softShadows;
        int shadowsEnabled;
        float _pad[3];
    };

    struct ShadowFrameResources {
        VkDescriptorSet pbrSet{VK_NULL_HANDLE};
        VkBuffer instanceBuffer{VK_NULL_HANDLE};
    };

    struct ShadowPushConstants {
        Mat4 lightViewProj;
    };

    class ShadowSystem {
    public:
        explicit ShadowSystem(VulkanContext &ctx);

        ~ShadowSystem() = default;

        ShadowSystem(const ShadowSystem &) = delete;

        ShadowSystem &operator=(const ShadowSystem &) = delete;

        void SetGpuProfileCtx(MnrGpuProfileCtx ctx) { m_GpuProfileCtx = ctx; }

        void Init(VkDescriptorPool pool, const ShadowSettings &s,
                  VkDescriptorSetLayout pbrSetLayout);

        void Recreate(VkDescriptorPool pool, const ShadowSettings &s,
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
                        const ShadowSettings &s);

        void UpdatePbrDescriptorSetShadow(VkDescriptorSet pbrSet) const;

        static Mat4 ComputeLightViewProj(const Vec3 &lightDir);

        bool IsEnabled() const { return m_Enabled; }
        VkImageView GetShadowMapView() const { return m_ShadowMap.view; }
        VkSampler GetShadowSampler() const { return m_ShadowSampler; }

        VkPipeline GetPipeline() const;

        VkPipelineLayout GetPipelineLayout() const;

        VkDescriptorSetLayout GetMeshCullSetLayout() const { return m_ShadowMeshCullSetLayout; }
        const ShadowUniformData &GetUniform() const { return m_ShadowUniform; }

        VkBuffer GetUniformBufferHandle() const;

    private:
        void CreateResources(const ShadowSettings &s);

        void BuildPipeline(VkDescriptorSetLayout pbrSetLayout);

        void BuildMeshCullLayout(VkDescriptorPool pool);

        VulkanContext &m_Context;

        AllocatedImage m_ShadowMap{};
        VkSampler m_ShadowSampler{VK_NULL_HANDLE};
        Scope<Buffer> m_ShadowUniformBuffer;
        ShadowUniformData m_ShadowUniform{};
        bool m_Enabled{false};

        Scope<Pipeline> m_ShadowPipeline;
        VkDescriptorSetLayout m_ShadowMeshCullSetLayout{VK_NULL_HANDLE};

        MnrGpuProfileCtx m_GpuProfileCtx{};
    };
} // namespace Manro