#pragma once

#include <Manro/Render/Renderer.h>

namespace Manro {
    class CVirtualFS;

    Scope<CRendererImpl> CreateRendererImpl(IWindow &window, CVirtualFS &vfs, u32 width, u32 height,
                                            const RenderSettings_t &settings, const RendererConfig_t &config);

    bool RendererImplBeginFrame(CRendererImpl & impl);

    void RendererImplBeginRendering(CRendererImpl & impl);

    void RendererImplRenderQueue(CRendererImpl & impl);

    void RendererImplEndRendering(CRendererImpl & impl);

    void RendererImplEndFrameAndPresent(CRendererImpl & impl);

    void RendererImplDrawMesh(CRendererImpl &impl, MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model);

    void RendererImplDrawMeshStatic(CRendererImpl &impl, MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model);

    void RendererImplClearStaticDraws(CRendererImpl & impl);

    void RendererImplDrawModel(CRendererImpl &impl, const CModel &model, const Mat4 &transform);

    void RendererImplDrawModelStatic(CRendererImpl &impl, const CModel &model, const Mat4 &transform);

    void RendererImplAddLight(CRendererImpl &impl, const LightData &light);

    void RendererImplClearLights(CRendererImpl & impl);

    void RendererImplSetViewProjection(CRendererImpl &impl, const Mat4 &view, const Mat4 &proj);

    void RendererImplSetCameraPosition(CRendererImpl &impl, const Vec3 &pos);

    void RendererImplSetSkybox(CRendererImpl &impl, TextureHandle cubemap);

    MeshHandle RendererImplUploadMesh(CRendererImpl &impl, const ModelData_t &data);

    TextureHandle RendererImplUploadTexture(CRendererImpl &impl, const TextureData_t &data);

    TextureHandle RendererImplUploadCubemap(CRendererImpl &impl, const std::vector<TextureData_t> &faces);

    Ref<CMaterial> RendererImplGetDefaultMaterial(const CRendererImpl &impl);

    Scope<CMaterialInstance> RendererImplCreateMaterialInstance(CRendererImpl &impl, const Ref<CMaterial> &mat);

    void RendererImplOnResize(CRendererImpl &impl, u32 width, u32 height);

    float RendererImplGetAspectRatio(const CRendererImpl &impl);

    CMeshManager &RendererImplGetMeshes(CRendererImpl & impl);

    void RendererImplSetSettings(CRendererImpl &impl, const RenderSettings_t &settings);

    const RenderSettings_t &RendererImplGetSettingsConst(const CRendererImpl &impl);

    RenderSettings_t &RendererImplGetSettings(CRendererImpl & impl);

    const RendererConfig_t &RendererImplGetConfig(const CRendererImpl &impl);

    const FrameStats_t &RendererImplGetLastFrameStats(const CRendererImpl &impl);

    void RendererImplSetDebugUIEnabled(const CRendererImpl &impl, bool enabled);

    bool RendererImplIsDebugUIEnabled(const CRendererImpl &impl);

    void RendererImplGetVramStats(const CRendererImpl &impl, u64 &usage, u64 &budget);

    std::string RendererImplGetAdapterName(const CRendererImpl &impl);

    void RendererImplDrawLine(const CRendererImpl &impl, const Vec3 &a, const Vec3 &b, u32 color, bool depthTest);

    void RendererImplDrawAABB(const CRendererImpl &impl, const Vec3 &min, const Vec3 &max, u32 color, bool depthTest);

    void RendererImplDrawBox(const CRendererImpl &impl, const Vec3 &center, const Vec3 &half, const Mat4 &transform,
                             u32 color, bool depthTest);

    void RendererImplDrawSphere(const CRendererImpl &impl, const Vec3 &center, float radius, u32 color, int segments,
                                bool depthTest);

    void RendererImplDrawFrustum(const CRendererImpl &impl, const Mat4 &invViewProj, u32 color, bool depthTest);

    void RendererImplDrawCross(const CRendererImpl &impl, const Vec3 &center, float size, u32 color, bool depthTest);

    void RendererImplDrawAxes(const CRendererImpl &impl, const Mat4 &transform, float size);

    void *RendererImplGetSceneTextureId(CRendererImpl & impl);

    void RendererImplWaitIdle(CRendererImpl & impl);
}
