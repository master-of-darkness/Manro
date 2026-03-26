#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/RenderGraph.h>
#include <Manro/Render/TextureManager.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Gui/ImGuiLayer.h>
#include <Manro/Render/Material/Material.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Platform/Window/IWindow.h>
#include <Manro/Core/VirtualFS.h>
#include <nvshaders/gltf_scene_io.h.slang>
#include <Manro/Render/Tonemap/Tonemapper.h>
#include <Manro/Render/RenderSettings.h>

#include <array>
#include <unordered_map>
#include <vector>

namespace Manro {
    class Model;

    class RendererImpl;

    static constexpr u32 MAX_FRAMES_IN_FLIGHT = 3;
    static constexpr u32 MAX_INSTANCES = 65536;
    static constexpr u32 MAX_LIGHTS = 1024;
    static constexpr u32 MAX_LIGHTS_PER_TILE = 64;
    static constexpr u32 TILE_SIZE = 16;
    static constexpr u32 SHADOW_MAP_SIZE = 2048;

    struct FrameStats {
        u32 drawCalls = 0;
        u32 triangleCount = 0;
        u32 instanceCount = 0;
        u32 lightCount = 0;
        void Reset() { drawCalls = triangleCount = instanceCount = lightCount = 0; }
    };

    class IWindow;

    using LightData = shaderio::GltfLight;

    class Renderer {
    public:
        Renderer(IWindow &window, u32 width, u32 height,
                 const RenderSettings &settings = {});

        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;

        bool BeginFrame();

        void BeginRendering(Vec4 clearColor);

        void RenderQueue();

        void EndRendering();

        void EndFrameAndPresent();

        void DrawMesh(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

        void DrawMeshStatic(MeshHandle mesh, MaterialInstance &mat, const Mat4 &model);

        void DrawModel(const Model &model, const Mat4 &transform);

        void DrawModelStatic(const Model &model, const Mat4 &transform);

        void AddLight(const LightData &light);

        void ClearLights();

        void SetViewProjection(const Mat4 &view, const Mat4 &proj);

        void SetCameraPosition(const Vec3 &pos);

        MeshHandle UploadMesh(const ModelData &data);

        TextureHandle UploadTexture(const TextureData &data);

        Ref<Material> GetDefaultMaterial() const;

        Scope<MaterialInstance> CreateMaterialInstance(Ref<Material> mat);

        void OnResize(u32 width, u32 height);

        float GetAspectRatio() const;

        TextureManager &GetTextures();

        MeshManager &GetMeshes();

        void SetSettings(const RenderSettings &settings);

        const RenderSettings &GetSettings() const;

        RenderSettings &GetSettings();

        const FrameStats &GetLastFrameStats() const;

        void GetVramStats(u64 &usage, u64 &budget) const;

        std::string GetAdapterName() const;

    private:
        Scope<RendererImpl> m_Impl;
    };
} // namespace Manro
