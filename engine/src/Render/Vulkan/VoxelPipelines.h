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
                             const std::vector<u8> &fragOpaqueSpv, const std::vector<u8> &fragCutoutSpv,
                             const std::vector<u8> &editSpv,
                             const std::vector<u8> &giInjectSpv, const std::vector<u8> &giPropSpv,
                             Scope<CPipeline> &outOpaque, Scope<CPipeline> &outCutout,
                             Scope<CPipeline> &outEdit,
                             Scope<CPipeline> &outGiInject, Scope<CPipeline> &outGiPropagate);
}
