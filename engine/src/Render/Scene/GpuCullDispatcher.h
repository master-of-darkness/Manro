#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/RenderSettings.h>
#include <Manro/Render/Renderer.h>

#include "../Internal/ShaderTypes.h"
#include "../Internal/FrameData.h"
#include "../Math/RenderMathUtils.h"
#include "../../Core/Profiling.h"

#include <volk.h>
#include <vector>

namespace Manro {
    class CVulkanContext;
    class CPipeline;
    class CPipelineCache;
    class CShadowSystem;
    class CMeshManager;

    class CGpuCullDispatcher {
    public:
        explicit CGpuCullDispatcher(CVulkanContext &ctx);

        void Init();

        void Shutdown();

        void BuildPipelines(CPipelineCache &cache);

        [[nodiscard]] VkDescriptorSetLayout GetCullSetLayout() const { return m_CullSetLayout; }

        [[nodiscard]] VkDescriptorSetLayout GetMeshCullSetLayout() const { return m_MeshCullSetLayout; }

        void SetGpuProfileCtx(MnrGpuProfileCtx ctx) { m_GpuProfileCtx = ctx; }

        struct DispatchParams_t {
            VkCommandBuffer cb;
            FrameData_t &frame;
            u32 totalInstCount;
            const Mat4 &viewMatrix;
            const Mat4 &projectionMatrix;
            const Vec3 &cameraPosition;
            const RenderSettings_t &settings;
            CShadowSystem &shadow;
            const std::vector<LightData> &lights;
            CMeshManager &meshes;
            VkExtent2D renderExtent;
            u32 maxLightsPerTile;
            u32 maxTilesX;
            u32 maxTilesY;
            u32 tileSize;
        };

        void Dispatch(const DispatchParams_t &params);

    private:
        void DispatchMeshCull(VkCommandBuffer cb, FrameData_t &frame,
                              u32 totalInstCount, const Mat4 &viewProj,
                              const Vec3 &cameraPosition, const RenderSettings_t &settings);

        void DispatchShadowCull(VkCommandBuffer cb, FrameData_t &frame,
                                u32 totalInstCount, CShadowSystem &shadow,
                                const std::vector<LightData> &lights,
                                const Vec3 &cameraPosition, const RenderSettings_t &settings,
                                CMeshManager &meshes);

        void DispatchLightTileCull(VkCommandBuffer cb, FrameData_t &frame,
                                   const Mat4 &viewMatrix, const Mat4 &projectionMatrix,
                                   const std::vector<LightData> &lights,
                                   VkExtent2D renderExtent,
                                   u32 maxLightsPerTile, u32 maxTilesX, u32 maxTilesY, u32 tileSize,
                                   const RenderSettings_t &settings);

        CVulkanContext &m_Context;

        Scope<CPipeline> m_CullPipeline;
        Scope<CPipeline> m_MeshCullPipeline;
        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_MeshCullSetLayout = VK_NULL_HANDLE;

        MnrGpuProfileCtx m_GpuProfileCtx{};
    };
} // namespace Manro