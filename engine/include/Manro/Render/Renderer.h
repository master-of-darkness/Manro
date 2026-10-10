#pragma once

#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/RendererConfig.h>
#include <Manro/Render/RenderSettings.h>
#include <Manro/Render/VoxelStreamStats.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Render/LightData.h>

#include <string>
#include <vector>

namespace Manro {
    class CWindow;
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
        float paceFenceMs = 0.f;
        float paceAcquireMs = 0.f;
        float pacePresentMs = 0.f;

        void Reset() {
            drawCalls = triangleCount = instanceCount = lightCount = 0;
            paceFenceMs = 0.f;
            paceAcquireMs = 0.f;
            pacePresentMs = 0.f;
        }
    };

    class CRenderer {
    public:
        /// Constructor with default RendererConfig_t
        CRenderer(CWindow &window, CVirtualFS &vfs, u32 width, u32 height,
                  const RenderSettings_t &settings = {});

        /// Constructor with custom RendererConfig_t
        CRenderer(CWindow &window, CVirtualFS &vfs, u32 width, u32 height,
                  const RenderSettings_t &settings,
                  const RendererConfig_t &config);

        ~CRenderer();

        CRenderer(const CRenderer &) = delete;

        CRenderer &operator=(const CRenderer &) = delete;

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

        void VoxelInit(u32 width, u32 height) const;
        void VoxelShutdown() const;
        void VoxelAllocateBrick(u32 bx, u32 by, u32 bz) const;
        void VoxelUploadBrick(u32 brickIdx, const u16 *mats, const u32 *occupancy) const;
        void VoxelQueueEdit(const Vec3 &pos, float radius, u32 material, u32 op) const;
        [[nodiscard]] u32 VoxelGetBrickCount() const;
        [[nodiscard]] u32 VoxelGetTaskGroups() const;
        void VoxelGetGpuTimes(float &xferMs, float &drawMs, float &postMs) const;
        void VoxelGetGpuInstantTimes(float &xferMs, float &drawMs, float &postMs) const;
        void VoxelSetDebugEnabled(bool enabled) const;
        void VoxelSetBackfaceEnabled(bool enabled) const;
        void VoxelSetFrustumEnabled(bool enabled) const;
        Vec3 VoxelStreamInit(const char *worldDir, const char *assetsDir, int radiusSections) const;
        int VoxelStreamUpdate() const;
        void VoxelStreamGetStats(VoxelStreamStats_t &out) const;
        bool VoxelStreamIsSolid(const Vec3 &p) const;
        bool VoxelStreamIsFluid(const Vec3 &p) const;
        u32 VoxelStreamPlaceState() const;
        i32 VoxelStreamGetStateAt(const Vec3 &p) const;
        bool VoxelStreamGetCollisionBox(const Vec3 &p, Vec3 &mn, Vec3 &mx) const;
        std::string VoxelGetStateKey(u32 id) const;
        u32 VoxelFindStateByKey(const std::string &key) const;
        void VoxelGetDebugCounters(u32 out[6]) const;

        void SetSettings(const RenderSettings_t &settings) const;

        const RenderSettings_t &GetSettings() const;

        RenderSettings_t &GetSettings();

        const FrameStats_t &GetLastFrameStats() const;

        void SetDebugUIEnabled(bool enabled) const;

        [[nodiscard]] bool IsDebugUIEnabled() const;

        void DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest = true) const;

        void DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest = true) const;

        void DrawBox(const Vec3 &center, const Vec3 &half, const Mat4 &transform,
                     u32 color, bool depthTest = true) const;

        void DrawSphere(const Vec3 &center, float radius, u32 color,
                        int segments = 8, bool depthTest = true) const;

        void *GetSceneTextureId() const;

        void WaitIdle() const;

    private:
        Scope<CRendererImpl> m_Impl;
    };
} // namespace Manro