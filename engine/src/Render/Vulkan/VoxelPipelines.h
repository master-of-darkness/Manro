#pragma once

#include <Manro/Core/Types.h>
#include <vector>

namespace Manro {
    class CVulkanContext;
    class CPipelineCache;
    class CTileArray;
    class CPipeline;

    void BuildVoxelPipelines(CVulkanContext &ctx, CPipelineCache &cache, CTileArray &tiles,
                             const std::vector<u8> &taskSpv, const std::vector<u8> &meshSpv,
                             const std::vector<u8> &fragSpv, const std::vector<u8> &editSpv,
                             const std::vector<u8> &giInjectSpv, const std::vector<u8> &giPropSpv,
                             Scope<CPipeline> &outMesh, Scope<CPipeline> &outEdit,
                             Scope<CPipeline> &outGiInject, Scope<CPipeline> &outGiPropagate);
}
