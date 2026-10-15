#include "VoxelPipelines.h"
#include "VulkanContext.h"
#include "Pipeline.h"
#include "PipelineCache.h"
#include "TileArray.h"
#include "../Voxel/VoxelTypes.h"

#include <Manro/Core/Logger.h>

namespace Manro {
    void BuildVoxelPipelines(CVulkanContext &ctx, CPipelineCache &cache, CTileArray &tiles,
                             const std::vector<u8> &taskSpv, const std::vector<u8> &meshSpv,
                             const std::vector<u8> &fragSpv, const std::vector<u8> &editSpv,
                             const std::vector<u8> &giInjectSpv, const std::vector<u8> &giPropSpv,
                             Scope<CPipeline> &outMesh, Scope<CPipeline> &outEdit,
                             Scope<CPipeline> &outGiInject, Scope<CPipeline> &outGiPropagate) {
        PipelineConfigParams_t meshCfg{};
        meshCfg.vertexEntryPoint = "main";
        meshCfg.fragmentEntryPoint = "main";
        meshCfg.computeEntryPoint = "main";
        meshCfg.colorAttachmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        meshCfg.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        meshCfg.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        meshCfg.pushConstantStages = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT |
                                     VK_SHADER_STAGE_FRAGMENT_BIT;
        meshCfg.pushConstantSize = sizeof(VoxelFrameRoot_t);
        tiles.AppendTo(meshCfg);
        meshCfg.depthWriteEnable = VK_TRUE;
        meshCfg.depthCompareOp = VK_COMPARE_OP_LESS;

        PipelineKey_t meshKey{};
        meshKey.taskHash = CPipelineCache::HashSpirV(taskSpv);
        meshKey.meshHash = CPipelineCache::HashSpirV(meshSpv);
        meshKey.fragHash = CPipelineCache::HashSpirV(fragSpv);
        meshKey.variants = PipelineVariant_MeshTask;
        meshKey.colorFmt = meshCfg.colorAttachmentFormat;
        meshKey.depthFmt = meshCfg.depthAttachmentFormat;
        meshKey.pushConstantSize = meshCfg.pushConstantSize;
        if (tiles.Valid()) {
            meshKey.setLayoutCount = 1;
            meshKey.setLayoutHash = 0x7B9B2F4A8C1D3E55ull;
        }

        outMesh = CreateScope<CPipeline>(ctx);
        CPipeline *meshRaw = outMesh.get();
        cache.GetGraphics(meshKey, [&](VkPipelineCache) -> VkPipeline {
            meshRaw->BuildMeshTask(taskSpv, meshSpv, fragSpv, meshCfg);
            return meshRaw->GetHandle();
        });

        auto buildCompute = [&](Scope<CPipeline> &pipe, const std::vector<u8> &spv, u32 pushSize,
                                const char *name) {
            PipelineConfigParams_t cfg{};
            cfg.computeEntryPoint = "main";
            cfg.pushConstantSize = pushSize;
            PipelineKey_t key{};
            key.compHash = CPipelineCache::HashSpirV(spv);
            key.variants = PipelineVariant_Compute;
            key.pushConstantSize = pushSize;
            pipe = CreateScope<CPipeline>(ctx);
            CPipeline *raw = pipe.get();
            cache.GetCompute(key, [&](VkPipelineCache) -> VkPipeline {
                raw->BuildCompute(spv, cfg);
                return raw->GetHandle();
            });
            if (!raw->IsValid())
                LOG_ERROR("[CVoxelRenderer] Compute pipeline build failed: {}", name);
        };

        buildCompute(outEdit, editSpv, sizeof(VoxelEditPushConstants_t), "voxel_edit");
        buildCompute(outGiInject, giInjectSpv, sizeof(VoxelGiPushConstants_t), "voxel_gi_inject");
        buildCompute(outGiPropagate, giPropSpv, sizeof(VoxelGiPushConstants_t), "voxel_gi_propagate");
    }
}
