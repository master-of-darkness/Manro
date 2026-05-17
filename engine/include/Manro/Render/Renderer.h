#pragma once

#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/RendererConfig.h>
#include <Manro/Render/RenderSettings.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Render/LightData.h>

#include <string>
#include <vector>

namespace Manro {
    class IWindow;
    class CMaterial;
    class CMeshManager;
    class CModel;
    class CRendererImpl;
    class CVirtualFS;

    struct FrameStats_t {
        u32 drawCalls = 0;
        u32 triangleCount = 0;
        u32 instanceCount = 0;
        u32 lightCount = 0;

        void Reset() { drawCalls = triangleCount = instanceCount = lightCount = 0; }
    };

    class CRenderer {
    public:
        /// Constructor with default RendererConfig_t
        CRenderer(IWindow &window, CVirtualFS &vfs, u32 width, u32 height,
                  const RenderSettings_t &settings = {});

        /// Constructor with custom RendererConfig_t
        CRenderer(IWindow &window, CVirtualFS &vfs, u32 width, u32 height,
                  const RenderSettings_t &settings,
                  const RendererConfig_t &config);

        ~CRenderer();

        CRenderer(const CRenderer &) = delete;

        CRenderer &operator=(const CRenderer &) = delete;

        [[nodiscard]] bool BeginFrame() const;

        [[nodiscard]] bool BeginFramePace() const;

        void BeginFrameRecord() const;

        void BeginRendering() const;

        void RenderQueue() const;

        void EndRendering() const;

        void EndFrameAndPresent() const;

        void DrawMesh(MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model) const;

        void DrawMeshStatic(MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model) const;

        void ClearStaticDraws() const;

        void DrawModel(const CModel &model, const Mat4 &transform) const;

        void DrawModelStatic(const CModel &model, const Mat4 &transform) const;

        void AddLight(const LightData &light) const;

        void ClearLights() const;

        void SetViewProjection(const Mat4 &view, const Mat4 &proj) const;

        void SetCameraPosition(const Vec3 &pos) const;

        void SetSkybox(TextureHandle cubemap) const;

        [[nodiscard]] MeshHandle UploadMesh(const ModelData_t &data) const;

        [[nodiscard]] TextureHandle UploadTexture(const TextureData_t &data) const;

        [[nodiscard]] TextureHandle UploadCubemap(const std::vector<TextureData_t> &faces) const;

        [[nodiscard]] Ref<CMaterial> GetDefaultMaterial() const;

        [[nodiscard]] Scope<CMaterialInstance> CreateMaterialInstance(const Ref<CMaterial> &mat) const;

        void OnResize(u32 width, u32 height) const;

        [[nodiscard]] float GetAspectRatio() const;

        CMeshManager &GetMeshes() const;

        void SetSettings(const RenderSettings_t &settings) const;

        const RenderSettings_t &GetSettings() const;

        RenderSettings_t &GetSettings();

        /// Get the renderer configuration (read-only, set at construction time)
        const RendererConfig_t &GetConfig() const;

        const FrameStats_t &GetLastFrameStats() const;

        void SetDebugUIEnabled(bool enabled) const;

        [[nodiscard]] bool IsDebugUIEnabled() const;

        void GetVramStats(u64 &usage, u64 &budget) const;

        [[nodiscard]] std::string GetAdapterName() const;

        void DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest = true) const;

        void DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest = true) const;

        void DrawBox(const Vec3 &center, const Vec3 &half, const Mat4 &transform,
                     u32 color, bool depthTest = true) const;

        void DrawSphere(const Vec3 &center, float radius, u32 color,
                        int segments = 8, bool depthTest = true) const;

        void DrawFrustum(const Mat4 &invViewProj, u32 color, bool depthTest = true) const;

        void DrawCross(const Vec3 &center, float size, u32 color, bool depthTest = true) const;

        void DrawAxes(const Mat4 &transform, float size = 50.f) const;

        void *GetSceneTextureId() const;

        void WaitIdle() const;

    private:
        Scope<CRendererImpl> m_Impl;
    };
} // namespace Manro