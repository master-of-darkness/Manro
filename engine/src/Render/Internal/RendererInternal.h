#pragma once

#include <Manro/Render/Renderer.h>

namespace Manro {
    Scope<RendererImpl> CreateRendererImpl(IWindow &window, u32 width, u32 height,
                                           const RenderSettings &settings, const RendererConfig &config);

    bool RendererImplBeginFrame(RendererImpl & impl);
    void RendererImplBeginRendering(RendererImpl & impl);
    void RendererImplRenderQueue(RendererImpl & impl);
    void RendererImplEndRendering(RendererImpl & impl);
    void RendererImplEndFrameAndPresent(RendererImpl & impl);

    void RendererImplDrawMesh(RendererImpl &impl, MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

    void RendererImplDrawMeshStatic(RendererImpl &impl, MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

    void RendererImplDrawModel(RendererImpl &impl, const Model &model, const Mat4 &transform);

    void RendererImplDrawModelStatic(RendererImpl &impl, const Model &model, const Mat4 &transform);

    void RendererImplAddLight(RendererImpl &impl, const LightData &light);

    void RendererImplClearLights(RendererImpl & impl);

    void RendererImplSetViewProjection(RendererImpl &impl, const Mat4 &view, const Mat4 &proj);

    void RendererImplSetCameraPosition(RendererImpl &impl, const Vec3 &pos);

    void RendererImplSetSkybox(RendererImpl &impl, TextureHandle cubemap);

    MeshHandle RendererImplUploadMesh(RendererImpl &impl, const ModelData &data);

    TextureHandle RendererImplUploadTexture(RendererImpl &impl, const TextureData &data);

    TextureHandle RendererImplUploadCubemap(RendererImpl &impl, const std::vector<TextureData> &faces);

    Ref<Material> RendererImplGetDefaultMaterial(const RendererImpl &impl);

    Scope<MaterialInstance> RendererImplCreateMaterialInstance(RendererImpl &impl, Ref<Material> mat);

    void RendererImplOnResize(RendererImpl &impl, u32 width, u32 height);

    float RendererImplGetAspectRatio(const RendererImpl &impl);

    TextureManager &RendererImplGetTextures(RendererImpl & impl);
    MeshManager &RendererImplGetMeshes(RendererImpl & impl);

    void RendererImplSetSettings(RendererImpl &impl, const RenderSettings &settings);

    const RenderSettings &RendererImplGetSettingsConst(const RendererImpl &impl);

    RenderSettings &RendererImplGetSettings(RendererImpl & impl);

    const RendererConfig &RendererImplGetConfig(const RendererImpl &impl);

    const FrameStats &RendererImplGetLastFrameStats(const RendererImpl &impl);

    void RendererImplSetDebugUIEnabled(RendererImpl &impl, bool enabled);

    bool RendererImplIsDebugUIEnabled(const RendererImpl &impl);

    void RendererImplGetVramStats(const RendererImpl &impl, u64 &usage, u64 &budget);

    std::string RendererImplGetAdapterName(const RendererImpl &impl);

    void RendererImplDebugLine(RendererImpl &impl, const Vec3 &a, const Vec3 &b, u32 color, bool depthTest);

    void RendererImplDebugAABB(RendererImpl &impl, const Vec3 &min, const Vec3 &max, u32 color, bool depthTest);

    void RendererImplDebugBox(RendererImpl &impl, const Vec3 &center, const Vec3 &half, const Mat4 &transform,
                              u32 color, bool depthTest);

    void RendererImplDebugSphere(RendererImpl &impl, const Vec3 &center, float radius, u32 color, int segments,
                                 bool depthTest);

    void RendererImplDebugFrustum(RendererImpl &impl, const Mat4 &invViewProj, u32 color, bool depthTest);

    void RendererImplDebugCross(RendererImpl &impl, const Vec3 &center, float size, u32 color, bool depthTest);

    void RendererImplDebugAxes(RendererImpl &impl, const Mat4 &transform, float size);
}