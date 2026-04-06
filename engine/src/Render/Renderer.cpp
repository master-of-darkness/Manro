#include "Internal/RendererInternal.h"

#include <Manro/Render/Renderer.h>
#include <Manro/Render/Material/Material.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/RendererConfig.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>

namespace Manro {
    bool Renderer::BeginFrame() { return RendererImplBeginFrame(*m_Impl); }

    void Renderer::BeginRendering() { RendererImplBeginRendering(*m_Impl); }

    void Renderer::RenderQueue() { RendererImplRenderQueue(*m_Impl); }

    void Renderer::EndRendering() { RendererImplEndRendering(*m_Impl); }

    void Renderer::EndFrameAndPresent() { RendererImplEndFrameAndPresent(*m_Impl); }

    void Renderer::DrawMesh(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model) {
        RendererImplDrawMesh(*m_Impl, mesh, mat, model);
    }

    void Renderer::DrawMeshStatic(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model) {
        RendererImplDrawMeshStatic(*m_Impl, mesh, mat, model);
    }

    void Renderer::ClearStaticDraws() { RendererImplClearStaticDraws(*m_Impl); }

    void Renderer::DrawModel(const Model &model, const Mat4 &transform) {
        RendererImplDrawModel(*m_Impl, model, transform);
    }

    void Renderer::DrawModelStatic(const Model &model, const Mat4 &transform) {
        RendererImplDrawModelStatic(*m_Impl, model, transform);
    }

    void Renderer::AddLight(const LightData &light) { RendererImplAddLight(*m_Impl, light); }

    void Renderer::ClearLights() { RendererImplClearLights(*m_Impl); }

    void Renderer::SetViewProjection(const Mat4 &view, const Mat4 &proj) {
        RendererImplSetViewProjection(*m_Impl, view, proj);
    }

    void Renderer::SetCameraPosition(const Vec3 &pos) { RendererImplSetCameraPosition(*m_Impl, pos); }

    void Renderer::SetSkybox(TextureHandle cubemap) { RendererImplSetSkybox(*m_Impl, cubemap); }

    MeshHandle Renderer::UploadMesh(const ModelData &data) { return RendererImplUploadMesh(*m_Impl, data); }

    TextureHandle Renderer::UploadTexture(const TextureData &data) { return RendererImplUploadTexture(*m_Impl, data); }

    TextureHandle Renderer::UploadCubemap(const std::vector<TextureData> &faces) {
        return RendererImplUploadCubemap(*m_Impl, faces);
    }

    Ref<Material> Renderer::GetDefaultMaterial() const { return RendererImplGetDefaultMaterial(*m_Impl); }

    Scope<MaterialInstance> Renderer::CreateMaterialInstance(Ref<Material> mat) {
        return RendererImplCreateMaterialInstance(*m_Impl, mat);
    }

    void Renderer::OnResize(u32 width, u32 height) { RendererImplOnResize(*m_Impl, width, height); }

    float Renderer::GetAspectRatio() const { return RendererImplGetAspectRatio(*m_Impl); }

    MeshManager &Renderer::GetMeshes() { return RendererImplGetMeshes(*m_Impl); }

    void Renderer::SetSettings(const RenderSettings &settings) { RendererImplSetSettings(*m_Impl, settings); }

    const RenderSettings &Renderer::GetSettings() const { return RendererImplGetSettingsConst(*m_Impl); }

    RenderSettings &Renderer::GetSettings() { return RendererImplGetSettings(*m_Impl); }

    const RendererConfig &Renderer::GetConfig() const { return RendererImplGetConfig(*m_Impl); }

    const FrameStats &Renderer::GetLastFrameStats() const { return RendererImplGetLastFrameStats(*m_Impl); }

    void Renderer::SetDebugUIEnabled(bool enabled) { RendererImplSetDebugUIEnabled(*m_Impl, enabled); }

    bool Renderer::IsDebugUIEnabled() const { return RendererImplIsDebugUIEnabled(*m_Impl); }

    void Renderer::GetVramStats(u64 &usage, u64 &budget) const { RendererImplGetVramStats(*m_Impl, usage, budget); }

    std::string Renderer::GetAdapterName() const { return RendererImplGetAdapterName(*m_Impl); }

    void Renderer::DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) {
        RendererImplDrawLine(*m_Impl, a, b, color, depthTest);
    }

    void Renderer::DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) {
        RendererImplDrawAABB(*m_Impl, min, max, color, depthTest);
    }

    void Renderer::DrawBox(const Vec3 &center, const Vec3 &half,
                           const Mat4 &transform, u32 color, bool depthTest) {
        RendererImplDrawBox(*m_Impl, center, half, transform, color, depthTest);
    }

    void Renderer::DrawSphere(const Vec3 &center, float radius,
                              u32 color, int segments, bool depthTest) {
        RendererImplDrawSphere(*m_Impl, center, radius, color, segments, depthTest);
    }

    void Renderer::DrawFrustum(const Mat4 &invViewProj, u32 color, bool depthTest) {
        RendererImplDrawFrustum(*m_Impl, invViewProj, color, depthTest);
    }

    void Renderer::DrawCross(const Vec3 &center, float size, u32 color, bool depthTest) {
        RendererImplDrawCross(*m_Impl, center, size, color, depthTest);
    }

    void Renderer::DrawAxes(const Mat4 &transform, float size) {
        RendererImplDrawAxes(*m_Impl, transform, size);
    }
} // namespace Manro
