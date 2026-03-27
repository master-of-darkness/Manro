#pragma once

#include <Manro/Render/RHI/IRenderDevice.h>
#include <Manro/Render/RHI/VulkanCommandList.h>
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

        void SetZPrepassState(const RHI::VulkanZPrepassState *state);

        void SetPbrPassState(const RHI::VulkanPbrPassState *state);

        void SetSkyboxPassState(const Manro::RHI::VulkanSkyboxPassState *state);

        void SetCompositePassState(const RHI::VulkanCompositePassState *state);

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
        const RHI::VulkanZPrepassState *m_ZPrepassState{nullptr};
        const RHI::VulkanPbrPassState *m_PbrPassState{nullptr};
        const RHI::VulkanSkyboxPassState *m_SkyboxPassState{nullptr};
        const RHI::VulkanCompositePassState *m_CompositePassState{nullptr};
    };

} // namespace Manro