#pragma once

#include <Manro/Interfaces/IRenderDevice.h>
#include <Manro/Render/RHI/ScenePassState.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Core/Types.h>
#include <span>
#include <vector>

namespace Manro {
    class SceneRenderer {
    public:
        explicit SceneRenderer();

        void DrawMesh(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

        void DrawMeshStatic(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

        void Flush(RHI::ICommandList &cmd, const Mat4 &view, const Mat4 &proj,
                   const Vec3 &camPos, std::span<const LightData> lights);

        void SetZPrepassState(const RHI::ZPrepassPassState *state);

        void SetPbrPassState(const RHI::PbrPassState *state);

        void SetSkyboxPassState(const Manro::RHI::SkyboxPassState *state);

        void SetCompositePassState(const RHI::CompositePassState *state);

        void AddLight(const LightData &light);

        void ClearLights();

        MeshHandle UploadMesh(const ModelData &data);

        TextureHandle UploadTexture(const TextureData &data);

    private:
        struct DrawItem {
            MeshHandle mesh{kInvalidMesh};
            MaterialInstance *material{nullptr};
            Mat4 model{1.f};
            bool isStatic{false};
        };

        std::vector<DrawItem> m_DrawQueue;
        std::vector<LightData> m_Lights;
        const RHI::ZPrepassPassState *m_ZPrepassState{nullptr};
        const RHI::PbrPassState *m_PbrPassState{nullptr};
        const RHI::SkyboxPassState *m_SkyboxPassState{nullptr};
        const RHI::CompositePassState *m_CompositePassState{nullptr};
    };
} // namespace Manro