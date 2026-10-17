#pragma once

#include "GpuDefs.h"
#include <volk.h>

namespace Manro {
    class CVulkanContext;
    class CBuffer;
    class CSparseBuffer;
    class CPipeline;
    class CTileArray;

    struct VoxelFrameBarriers {
        const CBuffer *params{nullptr};
        u64 paramsSize{0};
        const CBuffer *vis{nullptr};
        u64 visSize{0};
        const CBuffer *frag{nullptr};
        u64 fragSize{0};
        const CBuffer *headerOrNull{nullptr};
        const CSparseBuffer *bricksOrNull{nullptr};
        const CBuffer *faceCacheOrNull{nullptr};
        const CBuffer *spillOrNull{nullptr};
        const CBuffer *spillDescOrNull{nullptr};
        const CBuffer *debugOrNull{nullptr};
        bool debug{false};
    };

    class CVoxelEncoder {
    public:
        CVoxelEncoder(VkCommandBuffer cmd, VkImageView color, VkImageView depth, u32 width, u32 height,
                      bool clearColor);

        CVoxelEncoder(const CVoxelEncoder &) = delete;
        CVoxelEncoder &operator=(const CVoxelEncoder &) = delete;

        [[nodiscard]] VkCommandBuffer NativeCmd() const { return m_Cmd; }

        void FillBuffer(const CBuffer &buf, u64 offset, u64 size);
        void CopyStaging(const CBuffer &staging, CSparseBuffer &bricks, const BufferCopyRegion *regions,
                         u32 count);
        void BarrierEditUpload(const CBuffer &editStaging);
        void DispatchEdit(const CPipeline &pipe, const CBuffer &editStaging, const VoxelEditDispatch &desc);
        void DispatchGi(const CPipeline &pipe, const VoxelGiDispatch &desc, u32 groups);
        void BarrierGiToGi();
        void BarrierGiToFrag();
        void BarrierFrame(const VoxelFrameBarriers &bar);
        void BeginMeshPass();

        void BeginMeshPassResume();

        void DrawVoxelMesh(const CPipeline &pipe, const CTileArray &tiles, const void *rootBytes, u64 rootSize,
                           u32 groupCount);
        void EndMeshPass();
        void BarrierMeshToHost();

    private:
        void BeginRendering(bool clear);

        VkCommandBuffer m_Cmd{VK_NULL_HANDLE};
        VkImageView m_Color{VK_NULL_HANDLE};
        VkImageView m_Depth{VK_NULL_HANDLE};
        u32 m_Width{0};
        u32 m_Height{0};
        bool m_Clear{false};
    };
}
