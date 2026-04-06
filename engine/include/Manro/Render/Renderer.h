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
    class Material;
    class MeshManager;
    class Model;
    class RendererImpl;

    struct FrameStats {
        u32 drawCalls = 0;
        u32 triangleCount = 0;
        u32 instanceCount = 0;
        u32 lightCount = 0;

        void Reset() { drawCalls = triangleCount = instanceCount = lightCount = 0; }
    };

    using LightData = shaderio::GltfLight;

    class Renderer {
    public:
        /// Constructor with default RendererConfig
        Renderer(IWindow &window, u32 width, u32 height,
                 const RenderSettings &settings = {});

        /// Constructor with custom RendererConfig
        Renderer(IWindow &window, u32 width, u32 height,
                 const RenderSettings &settings,
                 const RendererConfig &config);

        ~Renderer();

        Renderer(const Renderer &) = delete;

        Renderer &operator=(const Renderer &) = delete;

        bool BeginFrame();

        void BeginRendering();

        void RenderQueue();

        void EndRendering();

        void EndFrameAndPresent();

        void DrawMesh(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

        void DrawMeshStatic(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

        void ClearStaticDraws();

        void DrawModel(const Model &model, const Mat4 &transform);

        void DrawModelStatic(const Model &model, const Mat4 &transform);

        void AddLight(const LightData &light);

        void ClearLights();

        void SetViewProjection(const Mat4 &view, const Mat4 &proj);

        void SetCameraPosition(const Vec3 &pos);

        void SetSkybox(TextureHandle cubemap);

        MeshHandle UploadMesh(const ModelData &data);

        TextureHandle UploadTexture(const TextureData &data);

        TextureHandle UploadCubemap(const std::vector<TextureData> &faces);

        Ref<Material> GetDefaultMaterial() const;

        Scope<MaterialInstance> CreateMaterialInstance(Ref<Material> mat);

        void OnResize(u32 width, u32 height);

        float GetAspectRatio() const;

        MeshManager &GetMeshes();

        void SetSettings(const RenderSettings &settings);

        const RenderSettings &GetSettings() const;

        RenderSettings &GetSettings();

        /// Get the renderer configuration (read-only, set at construction time)
        const RendererConfig &GetConfig() const;

        const FrameStats &GetLastFrameStats() const;

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
        Scope<RendererImpl> m_Impl;
    };
} // namespace Manro