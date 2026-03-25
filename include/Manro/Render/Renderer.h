#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/Vulkan/VulkanContext.h>
#include <Manro/Render/Vulkan/Swapchain.h>
#include <Manro/Render/Vulkan/Buffer.h>
#include <Manro/Render/Vulkan/Pipeline.h>
#include <Manro/Render/Vulkan/VulkanHelpers.h>
#include <Manro/Render/Vulkan/DescriptorAllocator.h>
#include <Manro/Render/Vulkan/PipelineCache.h>
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

    struct FrameData {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        Scope<Buffer> uboBuffer;
        Scope<Buffer> lightBuffer;
        Scope<Buffer> instanceBuffer;
        Scope<Buffer> cullDataBuffer;
        Scope<Buffer> indirectBuffer;
        Scope<Buffer> countBuffer;
        Scope<Buffer> tileHeaderBuffer;
        Scope<Buffer> tileLightIndexBuffer;
        Scope<Buffer> shadowIndirectBuffer;
        Scope<Buffer> shadowCountBuffer;

        VkDescriptorSet pbrSet = VK_NULL_HANDLE;
        VkDescriptorSet cullSet = VK_NULL_HANDLE;
        VkDescriptorSet meshCullSet = VK_NULL_HANDLE;
        VkDescriptorSet compositeSet = VK_NULL_HANDLE;
        VkDescriptorSet shadowMeshCullSet = VK_NULL_HANDLE;

        bool staticUploaded = false;
    };

    class IWindow;

    using LightData = shaderio::GltfLight;

    struct UniformBufferObject {
        Mat4 model;
        Mat4 view;
        Mat4 proj;
        Vec4 camPos;
        float exposure{1.0f};
        float gamma{2.2f};
        float prefilteredCubeMipLevels{1.0f};
        float scaleIBLAmbient{1.0f};
        int lightCount{0};
        int padding0{0};
        float padding1{0.0f};
        float padding2{0.0f};
        Vec2 screenDimensions;
        float nearZ{0.1f};
        float farZ{1000.0f};
        float slicesZ{1.0f};
        float _pad3{0.0f};
        Mat4 reflectionVP;
        int reflectionEnabled{0};
        int reflectionPass{0};
        Vec2 _reflectPad0;
        Vec4 clipPlaneWS;
        float reflectionIntensity{1.0f};
        int enableRayQueryReflections{0};
        int enableRayQueryTransparency{0};
        float _padReflect[1]{};
        int geometryInfoCount{0};
        int _padGeo[3]{};
        Vec4 _rqReservedWorldPos;
        int materialCount{0};
        int _padMat[3]{};
    };

    struct ShadowUniformData {
        Mat4 lightViewProj;
        Vec4 lightDir;
        Vec2 shadowMapSize;
        float normalBias;
        float _pad;
    };

    struct ShadowPushConstants {
        Mat4 lightViewProj;
    };

    struct PbrPushConstants {
        Vec4 baseColorFactor{1.f, 1.f, 1.f, 1.f};
        float metallicFactor{1.f};
        float roughnessFactor{1.f};
        int baseColorTextureSet{-1};
        int physicalDescriptorTextureSet{-1};
        int normalTextureSet{-1};
        int occlusionTextureSet{-1};
        int emissiveTextureSet{-1};
        float alphaMask{0.f};
        float alphaMaskCutoff{0.5f};
        float _pad0[3];
        Vec3 emissiveFactor{0.f, 0.f, 0.f};
        float emissiveStrength{1.f};
        float transmissionFactor{0.f};
        int useSpecGlossWorkflow{0};
        float glossinessFactor{1.f};
        float _pad1;
        Vec3 specularFactor{1.f, 1.f, 1.f};
        float ior{1.5f};
        int hasEmissiveStrengthExt{0};
        float _pad2;
    };

    struct MeshCullPushConstants {
        Vec4 planes[6];
        Vec4 cameraPos;
        u32 instanceCount;
        float maxDrawDistance;
        u32 _pad[2];
    };

    struct GpuCullData {
        float center[3];
        float radius;
        u32 instanceId;
        u32 _pad[3];
    };

    struct GpuMeshInstance {
        Mat4 modelMatrix;
        float normalMatrix[3][4];
        u32 materialIndex;
        u32 firstVertex;
        u32 firstIndex;
        u32 indexCount;
        float center[3];
        float radius;
        u32 flags;
        u32 _pad[3];
    };

    struct GpuDrawCommand {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        int vertexOffset;
        u32 firstInstance;
    };

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

        void SetViewProjection(const Mat4 &view, const Mat4 &proj) {
            m_ViewMatrix = view;
            m_ProjectionMatrix = proj;
        }

        void SetCameraPosition(const Vec3 &pos) { m_CameraPosition = pos; }

        MeshHandle UploadMesh(const ModelData &data) { return m_Meshes.Upload(data); }
        TextureHandle UploadTexture(const TextureData &data) { return m_Textures.Upload(data); }

        Ref<Material> GetDefaultMaterial() const { return m_DefaultMaterial; }

        Scope<MaterialInstance> CreateMaterialInstance(Ref<Material> mat);

        void OnResize(u32 width, u32 height);

        float GetAspectRatio() const {
            if (m_PendingHeight == 0) return 1.f;
            return static_cast<float>(m_PendingWidth) / static_cast<float>(m_PendingHeight);
        }

        VulkanContext &GetContext() { return m_Context; }
        TextureManager &GetTextures() { return m_Textures; }

        void SetSettings(const RenderSettings &settings);

        const RenderSettings &GetSettings() const { return m_Settings; }
        RenderSettings &GetSettings() { return m_Settings; }

        const FrameStats &GetLastFrameStats() const { return m_LastFrameStats; }

        PipelineCache &GetPipelineCache() { return m_PipelineCache; }
        RenderGraph &GetRenderGraph() { return m_RenderGraph; }
        BindlessAllocator &GetBindlessAlloc() { return m_BindlessAlloc; }

        ShadowUniformData &GetShadowUniform() { return m_ShadowUniform; }

    private:
        void CreateOffscreenResources(u32 w, u32 h);

        void CreateDepthResources(u32 w, u32 h);

        void CreateColorResources(u32 w, u32 h);

        void CreateShadowResources();

        void CreateDescriptorLayouts();

        void CreateDescriptorPool();

        void CreateGpuBuffers();

        void CreateCommandBuffers();

        void CreateSyncObjects();

        void BuildPbrPipeline();

        void BuildCompositePipeline();

        void BuildCullPipeline();

        void BuildShadowPipeline();

        void UpdatePbrDescriptorSet(u32 fi);

        void UpdatePbrDescriptorSetShadow(u32 fi);

        void UpdateCompositeDescriptorSet(u32 fi);

        void RecreateSwapchain();

        void UploadLights(u32 frameIndex);

        void RenderShadowPass(VkCommandBuffer cb);

        Mat4 ComputeLightViewProj(const Vec3 &lightDir) const;

        VulkanContext m_Context;
        Scope<Swapchain> m_Swapchain;

        TextureManager m_Textures;
        MeshManager m_Meshes;

        PerFrameAllocator m_PerFrameAlloc[MAX_FRAMES_IN_FLIGHT];
        PersistentAllocator m_PersistentAlloc;
        BindlessAllocator m_BindlessAlloc;
        PipelineCache m_PipelineCache;
        RenderGraph m_RenderGraph;

        Scope<ImGuiLayer> m_GuiLayer;
        Ref<Material> m_DefaultMaterial;

        std::vector<GpuMeshInstance> m_StaticInstances;
        std::vector<GpuCullData> m_StaticCullData;

        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_PbrSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CompositeSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_MeshCullSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_ShadowMeshCullSetLayout = VK_NULL_HANDLE;

        Scope<Pipeline> m_PbrPipeline;
        Scope<Pipeline> m_ZPrepassPipeline;
        Scope<Pipeline> m_CompositePipeline;
        Scope<Pipeline> m_CullPipeline;
        Scope<Pipeline> m_MeshCullPipeline;
        Scope<Pipeline> m_ShadowPipeline;

        AllocatedImage m_OffscreenColor{};
        AllocatedImage m_MsaaColorImage{};
        AllocatedImage m_DepthImage{};
        AllocatedImage m_ShadowMap{};

        VkSampler m_OffscreenSampler = VK_NULL_HANDLE;
        VkSampler m_ShadowSampler = VK_NULL_HANDLE;

        VkFormat m_OffscreenFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat m_DepthFormat = VK_FORMAT_D32_SFLOAT;

        RGTextureHandle m_RGOffscreen{};
        RGTextureHandle m_RGDepth{};
        RGTextureHandle m_RGSwapchain{};

        std::vector<FrameData> m_Frames;
        u32 m_CurrentFrame = 0;
        u32 m_CurrentImageIndex = 0;

        VkSemaphore m_TimelineSemaphore = VK_NULL_HANDLE;
        u64 m_TimelineValue = 0;
        std::array<u64, MAX_FRAMES_IN_FLIGHT> m_FrameBaseValue{};
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_PresentSemaphores;

        std::vector<MaterialData> m_Materials;
        std::unordered_map<MaterialData, u32, MaterialDataHash> m_MaterialCache;
        Scope<Buffer> m_MaterialBuffer;
        Scope<Buffer> m_TextureInfoBuffer;
        bool m_MaterialsDirty = true;

        Scope<Buffer> m_ShadowUniformBuffer;
        ShadowUniformData m_ShadowUniform{};

        std::vector<GpuMeshInstance> m_CurrentFrameInstances;
        std::vector<GpuCullData> m_CurrentFrameCullData;
        std::vector<LightData> m_PendingLights;

        Mat4 m_ViewMatrix = Mat4(1.f);
        Mat4 m_ProjectionMatrix = Mat4(1.f);
        Vec3 m_CameraPosition = Vec3(0.f);

        u32 m_PendingWidth = 0;
        u32 m_PendingHeight = 0;
        bool m_PendingResize = false;

        FrameStats m_CurrentFrameStats{};
        FrameStats m_LastFrameStats{};

        RenderSettings m_Settings{};
        VkExtent2D m_RenderExtent{};
        Vec4 m_CurrentClearColor{};
    };
} // namespace Manro
