#include "Internal/RendererBridge.h"

#include <Manro/Render/Renderer.h>
#include <Manro/Render/Material/Material.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/RendererConfig.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>

namespace Manro {
    bool CRenderer::BeginFrame() const { return RendererImplBeginFrame(*m_Impl); }

    bool CRenderer::BeginFramePace() const { return RendererImplBeginFramePace(*m_Impl); }

    void CRenderer::BeginFrameRecord() const { RendererImplBeginFrameRecord(*m_Impl); }

    void CRenderer::BeginRendering() const { RendererImplBeginRendering(*m_Impl); }

    void CRenderer::RenderQueue() const { RendererImplRenderQueue(*m_Impl); }

    void CRenderer::EndRendering() const { RendererImplEndRendering(*m_Impl); }

    void CRenderer::EndFrameAndPresent() const { RendererImplEndFrameAndPresent(*m_Impl); }

    void CRenderer::DrawMesh(MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model) const {
        RendererImplDrawMesh(*m_Impl, mesh, mat, model);
    }

    void CRenderer::DrawMeshStatic(MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model) const {
        RendererImplDrawMeshStatic(*m_Impl, mesh, mat, model);
    }

    void CRenderer::ClearStaticDraws() const { RendererImplClearStaticDraws(*m_Impl); }

    void CRenderer::DrawModel(const CModel &model, const Mat4 &transform) const {
        RendererImplDrawModel(*m_Impl, model, transform);
    }

    void CRenderer::DrawModelStatic(const CModel &model, const Mat4 &transform) const {
        RendererImplDrawModelStatic(*m_Impl, model, transform);
    }

    void CRenderer::AddLight(const LightData &light) const { RendererImplAddLight(*m_Impl, light); }

    void CRenderer::ClearLights() const { RendererImplClearLights(*m_Impl); }

    void CRenderer::SetViewProjection(const Mat4 &view, const Mat4 &proj) const {
        RendererImplSetViewProjection(*m_Impl, view, proj);
    }

    void CRenderer::SetCameraPosition(const Vec3 &pos) const { RendererImplSetCameraPosition(*m_Impl, pos); }

    void CRenderer::SetSkybox(TextureHandle cubemap) const { RendererImplSetSkybox(*m_Impl, cubemap); }

    MeshHandle CRenderer::UploadMesh(const ModelData_t &data) const { return RendererImplUploadMesh(*m_Impl, data); }

    TextureHandle CRenderer::UploadTexture(const TextureData_t &data) const {
        return RendererImplUploadTexture(*m_Impl, data);
    }

    TextureHandle CRenderer::UploadCubemap(const std::vector<TextureData_t> &faces) const {
        return RendererImplUploadCubemap(*m_Impl, faces);
    }

    Ref<CMaterial> CRenderer::GetDefaultMaterial() const { return RendererImplGetDefaultMaterial(*m_Impl); }

    Scope<CMaterialInstance> CRenderer::CreateMaterialInstance(const Ref<CMaterial> &mat) const {
        return RendererImplCreateMaterialInstance(*m_Impl, mat);
    }

    void CRenderer::OnResize(u32 width, u32 height) const { RendererImplOnResize(*m_Impl, width, height); }

    float CRenderer::GetAspectRatio() const { return RendererImplGetAspectRatio(*m_Impl); }

    CMeshManager &CRenderer::GetMeshes() const { return RendererImplGetMeshes(*m_Impl); }

    void CRenderer::SetSettings(const RenderSettings_t &settings) const { RendererImplSetSettings(*m_Impl, settings); }

    const RenderSettings_t &CRenderer::GetSettings() const { return RendererImplGetSettingsConst(*m_Impl); }

    RenderSettings_t &CRenderer::GetSettings() { return RendererImplGetSettings(*m_Impl); }

    const RendererConfig_t &CRenderer::GetConfig() const { return RendererImplGetConfig(*m_Impl); }

    const FrameStats_t &CRenderer::GetLastFrameStats() const { return RendererImplGetLastFrameStats(*m_Impl); }

    void CRenderer::SetDebugUIEnabled(bool enabled) const { RendererImplSetDebugUIEnabled(*m_Impl, enabled); }

    bool CRenderer::IsDebugUIEnabled() const { return RendererImplIsDebugUIEnabled(*m_Impl); }

    void CRenderer::GetVramStats(u64 &usage, u64 &budget) const { RendererImplGetVramStats(*m_Impl, usage, budget); }

    std::string CRenderer::GetAdapterName() const { return RendererImplGetAdapterName(*m_Impl); }

    void CRenderer::DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) const {
        RendererImplDrawLine(*m_Impl, a, b, color, depthTest);
    }

    void CRenderer::DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) const {
        RendererImplDrawAABB(*m_Impl, min, max, color, depthTest);
    }

    void CRenderer::DrawBox(const Vec3 &center, const Vec3 &half,
                            const Mat4 &transform, u32 color, bool depthTest) const {
        RendererImplDrawBox(*m_Impl, center, half, transform, color, depthTest);
    }

    void CRenderer::DrawSphere(const Vec3 &center, float radius,
                               u32 color, int segments, bool depthTest) const {
        RendererImplDrawSphere(*m_Impl, center, radius, color, segments, depthTest);
    }

    void CRenderer::DrawFrustum(const Mat4 &invViewProj, u32 color, bool depthTest) const {
        RendererImplDrawFrustum(*m_Impl, invViewProj, color, depthTest);
    }

    void CRenderer::DrawCross(const Vec3 &center, float size, u32 color, bool depthTest) const {
        RendererImplDrawCross(*m_Impl, center, size, color, depthTest);
    }

    void CRenderer::DrawAxes(const Mat4 &transform, float size) const {
        RendererImplDrawAxes(*m_Impl, transform, size);
    }

    void *CRenderer::GetSceneTextureId() const { return RendererImplGetSceneTextureId(*m_Impl); }

    void CRenderer::WaitIdle() const { RendererImplWaitIdle(*m_Impl); }
} // namespace Manro
