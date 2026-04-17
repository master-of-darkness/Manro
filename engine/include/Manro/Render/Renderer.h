#pragma once

#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/RendererConfig.h>
#include <Manro/Render/RenderSettings.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <nvshaders/gltf_scene_io.h.slang>

#include <string>
#include <vector>

namespace Manro {
    class IWindow;
    class CMaterial;
    class CMeshManager;
    class CModel;
    class CRendererImpl;

    struct FrameStats_t {
        u32 drawCalls = 0;
        u32 triangleCount = 0;
        u32 instanceCount = 0;
        u32 lightCount = 0;

        void Reset() { drawCalls = triangleCount = instanceCount = lightCount = 0; }
    };

    using LightData = shaderio::GltfLight;

    class CRenderer {
    public:
        /// Constructor with default RendererConfig_t
        CRenderer(IWindow &window, u32 width, u32 height,
                  const RenderSettings_t &settings = {});

        /// Constructor with custom RendererConfig_t
        CRenderer(IWindow &window, u32 width, u32 height,
                  const RenderSettings_t &settings,
                  const RendererConfig_t &config);

        ~CRenderer();

        CRenderer(const CRenderer &) = delete;

        CRenderer &operator=(const CRenderer &) = delete;

        bool BeginFrame();

        void BeginRendering();

        void RenderQueue();

        void EndRendering();

        void EndFrameAndPresent();

        void DrawMesh(MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model);

        void DrawMeshStatic(MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model);

        void ClearStaticDraws();

        void DrawModel(const CModel &model, const Mat4 &transform);

        void DrawModelStatic(const CModel &model, const Mat4 &transform);

        void AddLight(const LightData &light);

        void ClearLights();

        void SetViewProjection(const Mat4 &view, const Mat4 &proj);

        void SetCameraPosition(const Vec3 &pos);

        void SetSkybox(TextureHandle cubemap);

        MeshHandle UploadMesh(const ModelData_t &data);

        TextureHandle UploadTexture(const TextureData_t &data);

        TextureHandle UploadCubemap(const std::vector<TextureData_t> &faces);

        Ref<CMaterial> GetDefaultMaterial() const;

        Scope<CMaterialInstance> CreateMaterialInstance(Ref<CMaterial> mat);

        void OnResize(u32 width, u32 height);

        float GetAspectRatio() const;

        CMeshManager &GetMeshes();

        void SetSettings(const RenderSettings_t &settings);

        const RenderSettings_t &GetSettings() const;

        RenderSettings_t &GetSettings();

        /// Get the renderer configuration (read-only, set at construction time)
        const RendererConfig_t &GetConfig() const;

        const FrameStats_t &GetLastFrameStats() const;

        void SetDebugUIEnabled(bool enabled);

        bool IsDebugUIEnabled() const;

        void GetVramStats(u64 &usage, u64 &budget) const;

        std::string GetAdapterName() const;

        void DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest = true);

        void DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest = true);

        void DrawBox(const Vec3 &center, const Vec3 &half, const Mat4 &transform,
                     u32 color, bool depthTest = true);

        void DrawSphere(const Vec3 &center, float radius, u32 color,
                        int segments = 8, bool depthTest = true);

        void DrawFrustum(const Mat4 &invViewProj, u32 color, bool depthTest = true);

        void DrawCross(const Vec3 &center, float size, u32 color, bool depthTest = true);

        void DrawAxes(const Mat4 &transform, float size = 50.f);

    private:
        Scope<CRendererImpl> m_Impl;
    };
} // namespace Manro