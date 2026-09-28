#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>

#include "Core/Profiling.h"

namespace Manro::Internal {
    struct ZPrepassPassState_t;
    struct PbrPassState_t;
    struct SkyboxPassState_t;
    struct CompositePassState_t;
}

namespace Manro {
    class CSceneRenderer {
    public:
        explicit CSceneRenderer();

        void Flush(VkCommandBuffer cmd);

        // Optional voxel hook: invoked inside the PBR color/depth pass
        // (after vkCmdBeginRendering) so task/mesh draws share attachments.
        using VoxelRecordFn = void (*)(VkCommandBuffer cmd, void *user);
        void SetVoxelHook(VoxelRecordFn fn, void *user) {
            m_VoxelFn = fn;
            m_VoxelUser = user;
        }

        void SetZPrepassState(const Internal::ZPrepassPassState_t *state);

        void SetPbrPassState(const Internal::PbrPassState_t *state);

        void SetSkyboxPassState(const Internal::SkyboxPassState_t *state);

        void SetCompositePassState(const Internal::CompositePassState_t *state);

        void SetGpuProfileCtx(MnrGpuProfileCtx ctx) { m_GpuProfileCtx = ctx; }

    private:
        const Internal::ZPrepassPassState_t *m_ZPrepassState{nullptr};
        const Internal::PbrPassState_t *m_PbrPassState{nullptr};
        const Internal::SkyboxPassState_t *m_SkyboxPassState{nullptr};
        const Internal::CompositePassState_t *m_CompositePassState{nullptr};
        VoxelRecordFn m_VoxelFn{nullptr};
        void *m_VoxelUser{nullptr};

        MnrGpuProfileCtx m_GpuProfileCtx{};
    };
} // namespace Manro
