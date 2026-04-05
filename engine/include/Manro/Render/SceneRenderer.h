#pragma once

#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Core/Types.h>
#include <volk.h>
#include <span>
#include <vector>

namespace Manro::Internal {
    struct ZPrepassPassState;
    struct PbrPassState;
    struct SkyboxPassState;
    struct CompositePassState;
}

namespace Manro {
    class SceneRenderer {
    public:
        explicit SceneRenderer();

        void DrawMesh(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

        void DrawMeshStatic(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

        void Flush(VkCommandBuffer cmd, const Mat4 &view, const Mat4 &proj,
                   const Vec3 &camPos, std::span<const LightData> lights);

        void SetZPrepassState(const Internal::ZPrepassPassState *state);

        void SetPbrPassState(const Internal::PbrPassState *state);

        void SetSkyboxPassState(const Internal::SkyboxPassState *state);

        void SetCompositePassState(const Internal::CompositePassState *state);

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
        const Internal::ZPrepassPassState *m_ZPrepassState{nullptr};
        const Internal::PbrPassState *m_PbrPassState{nullptr};
        const Internal::SkyboxPassState *m_SkyboxPassState{nullptr};
        const Internal::CompositePassState *m_CompositePassState{nullptr};
    };
} // namespace Manro