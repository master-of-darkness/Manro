#include <Manro/Render/SceneRenderer.h>

namespace Manro {
    SceneRenderer::SceneRenderer() {
    }

    void SceneRenderer::DrawMesh(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model) {
        m_DrawQueue.push_back({mesh, &mat, model, false});
    }

    void SceneRenderer::DrawMeshStatic(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model) {
        m_DrawQueue.push_back({mesh, &mat, model, true});
    }

    void SceneRenderer::SetZPrepassState(const RHI::ZPrepassPassState *state) {
        m_ZPrepassState = state;
    }

    void SceneRenderer::SetPbrPassState(const RHI::PbrPassState *state) {
        m_PbrPassState = state;
    }

    void SceneRenderer::SetSkyboxPassState(const RHI::SkyboxPassState *state) {
        m_SkyboxPassState = state;
    }

    void SceneRenderer::SetCompositePassState(const RHI::CompositePassState *state) {
        m_CompositePassState = state;
    }

    void SceneRenderer::Flush(RHI::ICommandList &cmd, const Mat4 &view, const Mat4 &proj,
                              const Vec3 &camPos, std::span<const LightData> lights) {
        (void) view;
        (void) proj;
        (void) camPos;
        (void) lights;

        if (m_ZPrepassState) {
            cmd.ExecuteZPrepass(*m_ZPrepassState);
            m_ZPrepassState = nullptr;
        }

        if (m_PbrPassState) {
            cmd.ExecutePbrPass(*m_PbrPassState);
            m_PbrPassState = nullptr;
        }

        if (m_SkyboxPassState) {
            cmd.ExecuteSkyboxPass(*m_SkyboxPassState);
            m_SkyboxPassState = nullptr;
        }

        if (m_CompositePassState) {
            cmd.ExecuteCompositePass(*m_CompositePassState);
            m_CompositePassState = nullptr;
        }

        m_DrawQueue.clear();
    }

    void SceneRenderer::AddLight(const LightData &light) {
        m_Lights.push_back(light);
    }

    void SceneRenderer::ClearLights() {
        m_Lights.clear();
    }

    MeshHandle SceneRenderer::UploadMesh(const ModelData &data) {
        (void) data;
        return kInvalidMesh;
    }

    TextureHandle SceneRenderer::UploadTexture(const TextureData &data) {
        (void) data;
        return kInvalidTexture;
    }
} // namespace Manro