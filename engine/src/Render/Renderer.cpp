#include "Internal/RendererBridge.h"

#include <Manro/Render/Renderer.h>
#include <Manro/Render/Material/Material.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/RendererConfig.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>

namespace Manro {
    bool CRenderer::BeginFrame() { return RendererImplBeginFrame(*m_Impl); }

    void CRenderer::BeginRendering() { RendererImplBeginRendering(*m_Impl); }

    void CRenderer::RenderQueue() { RendererImplRenderQueue(*m_Impl); }

    void CRenderer::EndRendering() { RendererImplEndRendering(*m_Impl); }

    void CRenderer::EndFrameAndPresent() { RendererImplEndFrameAndPresent(*m_Impl); }

    void CRenderer::DrawMesh(MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model) {
        RendererImplDrawMesh(*m_Impl, mesh, mat, model);
    }

    void CRenderer::DrawMeshStatic(MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model) {
        RendererImplDrawMeshStatic(*m_Impl, mesh, mat, model);
    }

    void CRenderer::ClearStaticDraws() { RendererImplClearStaticDraws(*m_Impl); }

    void CRenderer::DrawModel(const CModel &model, const Mat4 &transform) {
        RendererImplDrawModel(*m_Impl, model, transform);
    }

    void CRenderer::DrawModelStatic(const CModel &model, const Mat4 &transform) {
        RendererImplDrawModelStatic(*m_Impl, model, transform);
    }

    void CRenderer::AddLight(const LightData &light) { RendererImplAddLight(*m_Impl, light); }

    void CRenderer::ClearLights() { RendererImplClearLights(*m_Impl); }

    void CRenderer::SetViewProjection(const Mat4 &view, const Mat4 &proj) {
        RendererImplSetViewProjection(*m_Impl, view, proj);
    }

    void CRenderer::SetCameraPosition(const Vec3 &pos) { RendererImplSetCameraPosition(*m_Impl, pos); }

    void CRenderer::SetSkybox(TextureHandle cubemap) { RendererImplSetSkybox(*m_Impl, cubemap); }

    MeshHandle CRenderer::UploadMesh(const ModelData_t &data) { return RendererImplUploadMesh(*m_Impl, data); }

    TextureHandle CRenderer::UploadTexture(const TextureData_t &data) {
        return RendererImplUploadTexture(*m_Impl, data);
    }

    TextureHandle CRenderer::UploadCubemap(const std::vector<TextureData_t> &faces) {
        return RendererImplUploadCubemap(*m_Impl, faces);
    }

    Ref<CMaterial> CRenderer::GetDefaultMaterial() const { return RendererImplGetDefaultMaterial(*m_Impl); }

    Scope<CMaterialInstance> CRenderer::CreateMaterialInstance(Ref<CMaterial> mat) {
        return RendererImplCreateMaterialInstance(*m_Impl, mat);
    }

    void CRenderer::OnResize(u32 width, u32 height) { RendererImplOnResize(*m_Impl, width, height); }

    float CRenderer::GetAspectRatio() const { return RendererImplGetAspectRatio(*m_Impl); }

    CMeshManager &CRenderer::GetMeshes() { return RendererImplGetMeshes(*m_Impl); }

    void CRenderer::SetSettings(const RenderSettings_t &settings) { RendererImplSetSettings(*m_Impl, settings); }

    const RenderSettings_t &CRenderer::GetSettings() const { return RendererImplGetSettingsConst(*m_Impl); }

    RenderSettings_t &CRenderer::GetSettings() { return RendererImplGetSettings(*m_Impl); }

    const RendererConfig_t &CRenderer::GetConfig() const { return RendererImplGetConfig(*m_Impl); }

    const FrameStats_t &CRenderer::GetLastFrameStats() const { return RendererImplGetLastFrameStats(*m_Impl); }

    void CRenderer::SetDebugUIEnabled(bool enabled) { RendererImplSetDebugUIEnabled(*m_Impl, enabled); }

    bool CRenderer::IsDebugUIEnabled() const { return RendererImplIsDebugUIEnabled(*m_Impl); }

    void CRenderer::GetVramStats(u64 &usage, u64 &budget) const { RendererImplGetVramStats(*m_Impl, usage, budget); }

    std::string CRenderer::GetAdapterName() const { return RendererImplGetAdapterName(*m_Impl); }

    void CRenderer::DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) {
        RendererImplDrawLine(*m_Impl, a, b, color, depthTest);
    }

    void CRenderer::DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) {
        RendererImplDrawAABB(*m_Impl, min, max, color, depthTest);
    }

    void CRenderer::DrawBox(const Vec3 &center, const Vec3 &half,
                            const Mat4 &transform, u32 color, bool depthTest) {
        RendererImplDrawBox(*m_Impl, center, half, transform, color, depthTest);
    }

    void CRenderer::DrawSphere(const Vec3 &center, float radius,
                               u32 color, int segments, bool depthTest) {
        RendererImplDrawSphere(*m_Impl, center, radius, color, segments, depthTest);
    }

    void CRenderer::DrawFrustum(const Mat4 &invViewProj, u32 color, bool depthTest) {
        RendererImplDrawFrustum(*m_Impl, invViewProj, color, depthTest);
    }

    void CRenderer::DrawCross(const Vec3 &center, float size, u32 color, bool depthTest) {
        RendererImplDrawCross(*m_Impl, center, size, color, depthTest);
    }

    void CRenderer::DrawAxes(const Mat4 &transform, float size) {
        RendererImplDrawAxes(*m_Impl, transform, size);
    }

    void *CRenderer::GetSceneTextureId() { return RendererImplGetSceneTextureId(*m_Impl); }

    void CRenderer::WaitIdle() { RendererImplWaitIdle(*m_Impl); }
} // namespace Manro
