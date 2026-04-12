#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/RenderSettings.h>
#include <Manro/Render/Renderer.h>
#include "RendererTypes.h"
#include "RenderMathUtils.h"

#include <volk.h>
#include <vector>

namespace Manro {
    class VulkanContext;
    class Pipeline;
    class PipelineCache;
    class ShadowSystem;
    class MeshManager;

    class GpuCullDispatcher {
    public:
        explicit GpuCullDispatcher(VulkanContext &ctx);

        void Init();

        void Shutdown();

        void BuildPipelines(PipelineCache &cache);

        [[nodiscard]] VkDescriptorSetLayout GetCullSetLayout() const { return m_CullSetLayout; }

        [[nodiscard]] VkDescriptorSetLayout GetMeshCullSetLayout() const { return m_MeshCullSetLayout; }

        struct DispatchParams {
            VkCommandBuffer cb;
            FrameData &frame;
            u32 totalInstCount;
            const Mat4 &viewMatrix;
            const Mat4 &projectionMatrix;
            const Vec3 &cameraPosition;
            const RenderSettings &settings;
            ShadowSystem &shadow;
            const std::vector<LightData> &lights;
            MeshManager &meshes;
            VkExtent2D renderExtent;
            u32 maxLightsPerTile;
            u32 maxTilesX;
            u32 maxTilesY;
            u32 tileSize;
        };

        void Dispatch(const DispatchParams &params);

    private:
        void DispatchMeshCull(VkCommandBuffer cb, FrameData &frame,
                              u32 totalInstCount, const Mat4 &viewProj,
                              const Vec3 &cameraPosition, const RenderSettings &settings);

        void DispatchShadowCull(VkCommandBuffer cb, FrameData &frame,
                                u32 totalInstCount, ShadowSystem &shadow,
                                const std::vector<LightData> &lights,
                                const Vec3 &cameraPosition, const RenderSettings &settings,
                                MeshManager &meshes);

        void DispatchLightTileCull(VkCommandBuffer cb, FrameData &frame,
                                   const Mat4 &viewMatrix, const Mat4 &projectionMatrix,
                                   const std::vector<LightData> &lights,
                                   VkExtent2D renderExtent,
                                   u32 maxLightsPerTile, u32 maxTilesX, u32 maxTilesY, u32 tileSize,
                                   const RenderSettings &settings);

        VulkanContext &m_Context;

        Scope<Pipeline> m_CullPipeline;
        Scope<Pipeline> m_MeshCullPipeline;
        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_MeshCullSetLayout = VK_NULL_HANDLE;
    };
} // namespace Manro