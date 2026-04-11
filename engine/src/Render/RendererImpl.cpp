#include "Internal/RendererInternal.h"
#include "Internal/SceneRenderer.h"
#include "Internal/MeshManagerInternal.h"
#include "Internal/SwapchainManager.h"
#include "Internal/RenderTargetManager.h"
#include "Internal/ShadowSystem.h"
#include "Internal/SkyboxRenderer.h"
#include "Vulkan/VulkanHelpers.h"
#include "Overlay/Overlay.h"
#include "Vulkan/VulkanContext.h"
#include "Vulkan/Buffer.h"
#include "Vulkan/Pipeline.h"
#include "Vulkan/DescriptorAllocator.h"
#include "Vulkan/PipelineCache.h"
#include "Material/Material.h"
#include "Texture/TextureManager.h"
#include "Internal/ScenePassState.h"
#include "Internal/DrawSystem.h"

#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/Model.h>
#include <Manro/Render/RendererConfig.h>
#include <VkBootstrap.h>
#include <stdexcept>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>


namespace Manro {
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
        VkDescriptorSet skyboxSet = VK_NULL_HANDLE;

        bool staticUploaded = false;
    };

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
        int shadowsEnabled{1};
        float aoIntensity{0.0f};
        float aoRadius{0.5f};
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
        int rayMaxBounces{1};
        int _padGeo[3]{};
        Vec4 _rqReservedWorldPos;
        int materialCount{0};
        int _padMat[3]{};
    };

    struct PBRPushConstants {
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
        u32 enableFrustumCulling;
        u32 _pad;
    };

    struct CullData {
        float center[3];
        float radius;
        u32 instanceId;
        u32 _pad[3];
    };

    struct MeshInstance {
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

    struct DrawCommand {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        int vertexOffset;
        u32 firstInstance;
    };

    class RendererImpl final {
    public:
        RendererImpl(IWindow &window, u32 width, u32 height, const RenderSettings &settings,
                     const RendererConfig &config = RendererConfig::Default());

        ~RendererImpl();

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

        void SetViewProjection(const Mat4 &view, const Mat4 &proj) {
            m_ViewMatrix = view;
            m_ProjectionMatrix = proj;
        }

        void SetCameraPosition(const Vec3 &pos) { m_CameraPosition = pos; }

        void SetSkybox(TextureHandle cubemap) {
            if (cubemap == kInvalidTexture) {
                LOG_ERROR("[Renderer] SetSkybox called with invalid texture!");
            } else {
                LOG_INFO("[Renderer] Skybox texture set: {}", cubemap);
            }
            std::vector<VkBuffer> uboBuffers;
            std::vector<VkDescriptorSet> skyboxSets;
            uboBuffers.reserve(m_Frames.size());
            skyboxSets.reserve(m_Frames.size());
            for (auto &f: m_Frames) {
                uboBuffers.push_back(f.uboBuffer->GetHandle());
                skyboxSets.push_back(f.skyboxSet);
            }
            m_Skybox.SetTexture(cubemap, m_Textures, uboBuffers, skyboxSets);
        }

        MeshHandle UploadMesh(const ModelData &data) { return m_Meshes.Upload(data); }

        TextureHandle UploadTexture(const TextureData &data) { return m_Textures.Upload(data); }

        TextureHandle UploadCubemap(const std::vector<TextureData> &faces) { return m_Textures.UploadCubemap(faces); }

        Ref<Material> GetDefaultMaterial() const { return m_DefaultMaterial; }


        static Scope<MaterialInstance> CreateMaterialInstance(const Ref<Material> &mat);

        void OnResize(u32 width, u32 height);

        float GetAspectRatio() const {
            if (m_PendingHeight == 0) return 1.f;
            return static_cast<float>(m_PendingWidth) / static_cast<float>(m_PendingHeight);
        }

        VulkanContext &GetContext() { return m_Context; }

        TextureManager &GetTextures() { return m_Textures; }

        MeshManager &GetMeshes() { return m_Meshes; }

        void SetSettings(const RenderSettings &settings);

        const RenderSettings &GetSettings() const { return m_Settings; }

        RenderSettings &GetSettings() { return m_Settings; }

        const RendererConfig &GetConfig() const { return m_Config; }

        const FrameStats &GetLastFrameStats() const { return m_LastFrameStats; }

        void SetDebugUIEnabled(bool enabled) const {
            if (m_Overlay) m_Overlay->SetDebugUIEnabled(enabled);
        }

        bool IsDebugUIEnabled() const {
            return m_Overlay && m_Overlay->IsDebugUIEnabled();
        }

        void GetVramStats(u64 &usage, u64 &budget) const {
            m_Context.GetVramStats(usage, budget);
        }

        std::string GetAdapterName() const {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(m_Context.GetPhysicalDevice(), &props);
            return props.deviceName;
        }

        void DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) const;

        void DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) const;

        void DrawBox(const Vec3 &center, const Vec3 &half, const Mat4 &transform, u32 color, bool depthTest) const;

        void DrawSphere(const Vec3 &center, float radius, u32 color, int segments, bool depthTest) const;

        void DrawFrustum(const Mat4 &invViewProj, u32 color, bool depthTest) const;

        void DrawCross(const Vec3 &center, float size, u32 color, bool depthTest) const;

        void DrawAxes(const Mat4 &transform, float size) const;

    private:
        void CreateDescriptorLayouts();

        void CreateDescriptorPool();

        void CreateToGpuBuffers();

        void CreateCommandBuffers();

        void CreateSyncObjects();

        void BuildPbrPipeline();

        void BuildCompositePipeline();

        void BuildCullPipeline();

        void UpdatePbrDescriptorSet(u32 fi);

        void UpdateCompositeDescriptorSet(u32 fi);

        void InitializeSwapchain(u32 width, u32 height, bool vsync);

        void RecreateSwapchain();

        void UploadLights(u32 frameIndex);

        void FinalizeFrameAndPresent(VkCommandBuffer cb);

        u32 GetFrameCount() const { return std::max(1u, m_Config.maxFramesInFlight); }
        u32 GetMaxInstances() const { return std::max(1u, m_Config.maxInstances); }
        u32 GetMaxLights() const { return std::max(1u, m_Config.maxLights); }
        u32 GetMaxLightsPerTile() const { return std::max(1u, m_Config.maxLightsPerTile); }
        u32 GetTileSize() const { return std::max(1u, m_Config.tileSize); }
        u32 GetShadowMapSize() const { return std::max(1u, m_Config.shadowMapSize); }
        u32 GetMaxTilesX() const { return std::max(1u, 4096u / GetTileSize()); }
        u32 GetMaxTilesY() const { return std::max(1u, 2304u / GetTileSize()); }
        u32 GetMaxTiles() const { return GetMaxTilesX() * GetMaxTilesY(); }

        VulkanContext m_Context;
        Scope<SceneRenderer> m_SceneRenderer;

        TextureManager m_Textures;
        MeshManager m_Meshes;

        std::vector<PerFrameAllocator> m_PerFrameAlloc;
        PersistentAllocator m_PersistentAlloc;
        BindlessAllocator m_BindlessAlloc;
        PipelineCache m_PipelineCache;

        Scope<Overlay> m_Overlay;
        Ref<Material> m_DefaultMaterial;

        std::vector<MeshInstance> m_StaticInstances;
        std::vector<CullData> m_StaticCullData;
        u32 m_StaticTriangleCount = 0;

        Scope<DrawSystem> m_DrawSystem;

        SwapchainManager m_Swapchain;
        RenderTargetManager m_RenderTargets;
        ShadowSystem m_Shadow;
        SkyboxRenderer m_Skybox;

        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_PbrSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CompositeSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_MeshCullSetLayout = VK_NULL_HANDLE;

        Scope<Pipeline> m_PbrPipeline;
        Scope<Pipeline> m_ZPrepassPipeline;
        Scope<Pipeline> m_CompositePipeline;
        Scope<Pipeline> m_CullPipeline;
        Scope<Pipeline> m_MeshCullPipeline;

        std::vector<FrameData> m_Frames;
        u32 m_CurrentFrame = 0;
        u32 m_CurrentImageIndex = 0;

        std::vector<MaterialData> m_Materials;
        std::unordered_map<MaterialData, u32, MaterialDataHash> m_MaterialCache;
        Scope<Buffer> m_MaterialBuffer;
        Scope<Buffer> m_TextureInfoBuffer;
        bool m_MaterialsDirty = true;

        std::vector<MeshInstance> m_CurrentFrameInstances;
        std::vector<CullData> m_CurrentFrameCullData;
        std::vector<LightData> m_PendingLights;

        Mat4 m_ViewMatrix = Mat4(1.f);
        Mat4 m_ProjectionMatrix = Mat4(1.f);
        Vec3 m_CameraPosition = Vec3(0.f);

        u32 m_PendingWidth = 0;
        u32 m_PendingHeight = 0;
        bool m_PendingResize = false;
        bool m_PendingShadowRecreate = false;

        FrameStats m_CurrentFrameStats{};
        FrameStats m_LastFrameStats{};

        RenderSettings m_Settings{};
        RendererConfig m_Config{};
        VkExtent2D m_RenderExtent{};

        void UpdateSkyboxDescriptorSet(u32 fi);

        static void ComputeNormalMatrix(MeshInstance &inst, const Mat4 &model);

        static void BuildCullData(CullData &out, const LoadedMesh *mesh, const Mat4 &model, u32 instanceId);

        static void ExtractFrustumPlanes(const Mat4 &viewProj, Vec4 planes[6]);

        u32 ResolveMaterialIndex(MaterialInstance &material);
    };

    static VkSampleCountFlagBits ToVulkanSampleCount(MSAASampleCount samples) {
        switch (samples) {
            case MSAASampleCount::MSAA_1X: return VK_SAMPLE_COUNT_1_BIT;
            case MSAASampleCount::MSAA_2X: return VK_SAMPLE_COUNT_2_BIT;
            case MSAASampleCount::MSAA_4X: return VK_SAMPLE_COUNT_4_BIT;
            case MSAASampleCount::MSAA_8X: return VK_SAMPLE_COUNT_8_BIT;
            case MSAASampleCount::MSAA_16X: return VK_SAMPLE_COUNT_16_BIT;
            case MSAASampleCount::MSAA_32X: return VK_SAMPLE_COUNT_32_BIT;
            case MSAASampleCount::MSAA_64X: return VK_SAMPLE_COUNT_64_BIT;
            default: return VK_SAMPLE_COUNT_1_BIT;
        }
    }

    static MSAASampleCount FromVulkanSampleCount(VkSampleCountFlagBits samples) {
        switch (samples) {
            case VK_SAMPLE_COUNT_1_BIT: return MSAASampleCount::MSAA_1X;
            case VK_SAMPLE_COUNT_2_BIT: return MSAASampleCount::MSAA_2X;
            case VK_SAMPLE_COUNT_4_BIT: return MSAASampleCount::MSAA_4X;
            case VK_SAMPLE_COUNT_8_BIT: return MSAASampleCount::MSAA_8X;
            case VK_SAMPLE_COUNT_16_BIT: return MSAASampleCount::MSAA_16X;
            case VK_SAMPLE_COUNT_32_BIT: return MSAASampleCount::MSAA_32X;
            case VK_SAMPLE_COUNT_64_BIT: return MSAASampleCount::MSAA_64X;
            default: return MSAASampleCount::MSAA_1X;
        }
    }

    static void NormalizeRenderSettings(RenderSettings &settings, VkSampleCountFlagBits maxSamples) {
        settings.resolutionScale = std::clamp(settings.resolutionScale, 0.1f, 2.0f);

        if (settings.aaMode != AntiAliasingMode::MSAA) {
            settings.msaaSamples = MSAASampleCount::MSAA_1X;
        } else if (static_cast<u32>(ToVulkanSampleCount(settings.msaaSamples)) > static_cast<u32>(maxSamples)) {
            settings.msaaSamples = FromVulkanSampleCount(maxSamples);
        }

        settings.nearZ = std::max(settings.nearZ, 0.001f);
        settings.farZ = std::max(settings.farZ, settings.nearZ + 0.001f);
        if (settings.maxDrawDistance <= 0.0f) {
            settings.maxDrawDistance = settings.farZ;
        } else {
            settings.maxDrawDistance = std::max(settings.maxDrawDistance, settings.nearZ);
        }

        settings.shadows.resolution = std::clamp(settings.shadows.resolution, 128, 8192);
        settings.shadows.bias = std::max(settings.shadows.bias, 0.0f);
        settings.shadows.slopeBias = std::max(settings.shadows.slopeBias, 0.0f);
        settings.shadows.softShadows = std::max(settings.shadows.softShadows, 0.0f);

        settings.lighting.iblIntensity = std::max(settings.lighting.iblIntensity, 0.0f);
        settings.lighting.gamma = std::clamp(settings.lighting.gamma, 1.0f, 3.0f);
        settings.lighting.aoIntensity = std::max(settings.lighting.aoIntensity, 0.0f);
        settings.lighting.aoRadius = std::max(settings.lighting.aoRadius, 0.0f);

        settings.postProcess.bloomIntensity = std::max(settings.postProcess.bloomIntensity, 0.0f);
        settings.postProcess.bloomThreshold = std::max(settings.postProcess.bloomThreshold, 0.0f);

        settings.rayTracing.maxBounces = std::clamp(settings.rayTracing.maxBounces, 1, 8);
    }

    RendererImpl::RendererImpl(IWindow &window, u32 width, u32 height,
                               const RenderSettings &settings,
                               const RendererConfig &config)
        : m_Context("GameEngine", window),
          m_Textures(m_Context, m_BindlessAlloc), m_Meshes(new MeshManagerImpl(m_Context)),
          m_Swapchain(m_Context), m_RenderTargets(m_Context), m_Shadow(m_Context), m_Skybox(m_Context),
          m_Settings(settings), m_Config(config) {
        NormalizeRenderSettings(m_Settings, m_Context.GetMaxUsableSampleCount());
        m_Swapchain.Init(width, height, m_Settings.enableVSync);

        m_PendingWidth = width;
        m_PendingHeight = height;
        m_RenderExtent.width = std::max(1u, static_cast<u32>(width * m_Settings.resolutionScale));
        m_RenderExtent.height = std::max(1u, static_cast<u32>(height * m_Settings.resolutionScale));

        VkDevice device = m_Context.GetDevice();

        m_PerFrameAlloc.resize(GetFrameCount());
        for (auto &alloc: m_PerFrameAlloc)
            alloc.Init(device, 256);

        m_PersistentAlloc.Init(device, 64);
        m_BindlessAlloc.Init(device);
        m_Textures.InitDefaults();
        m_PipelineCache.Init(device, "manro_pipeline_cache.bin");
        m_SceneRenderer = CreateScope<SceneRenderer>();

        m_RenderTargets.Create(m_RenderExtent.width, m_RenderExtent.height,
                               ToVulkanSampleCount(m_Settings.msaaSamples));
        CreateDescriptorLayouts();
        CreateDescriptorPool();

        m_Shadow.Init(m_DescriptorPool, m_Settings.shadows, m_PbrSetLayout);

        m_Skybox.Init(m_DescriptorPool, GetFrameCount(),
                      m_RenderTargets.GetOffscreenFormat(),
                      m_RenderTargets.GetDepthFormat(),
                      ToVulkanSampleCount(m_Settings.msaaSamples));

        CreateToGpuBuffers();
        BuildPbrPipeline();
        BuildCompositePipeline();
        BuildCullPipeline();

        m_DrawSystem = CreateScope<DrawSystem>(m_Context);
        m_DrawSystem->Init(m_RenderTargets.GetOffscreenFormat(),
                           m_RenderTargets.GetDepthFormat(),
                           ToVulkanSampleCount(m_Settings.msaaSamples));

        CreateCommandBuffers();
        m_Swapchain.CreateFrameSyncObjects(GetFrameCount());
        m_Swapchain.CreateRenderFinishedSemaphores();

        m_CurrentFrameInstances.reserve(GetMaxInstances());
        m_CurrentFrameCullData.reserve(GetMaxInstances());
        m_PendingLights.reserve(GetMaxLights());

        m_Materials.push_back(shaderio::defaultGltfMaterial());
        m_MaterialBuffer->LoadData(m_Materials.data(), sizeof(MaterialData));

        OverlayInfo guiInfo{};
        guiInfo.context = &m_Context;
        guiInfo.window = &window;
        guiInfo.colorFormat = m_Swapchain.GetFormat();
        guiInfo.imageCount = m_Swapchain.GetImageCount();
        m_Overlay = CreateScope<Overlay>(guiInfo);
    }

    RendererImpl::~RendererImpl() {
        if (!m_Context.GetDevice()) return;
        vkDeviceWaitIdle(m_Context.GetDevice());

        m_PipelineCache.Shutdown();
        m_BindlessAlloc.Shutdown();
        m_PersistentAlloc.Shutdown();
        for (auto &alloc: m_PerFrameAlloc)
            alloc.Shutdown();

        m_Overlay.reset();
        m_DefaultMaterial.reset();
        m_PbrPipeline.reset();
        m_ZPrepassPipeline.reset();
        m_CompositePipeline.reset();
        m_CullPipeline.reset();
        m_MeshCullPipeline.reset();

        m_Shadow.Shutdown();
        m_Skybox.Shutdown();
        m_RenderTargets.Destroy();

        m_Swapchain.DestroyFrameSyncObjects();

        for (auto &f: m_Frames)
            if (f.commandPool)
                vkDestroyCommandPool(m_Context.GetDevice(), f.commandPool, nullptr);

        m_Swapchain.Shutdown();

        if (m_DescriptorPool)
            vkDestroyDescriptorPool(m_Context.GetDevice(), m_DescriptorPool, nullptr);
        if (m_PbrSetLayout)
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_PbrSetLayout, nullptr);
        if (m_CompositeSetLayout)
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_CompositeSetLayout, nullptr);
        if (m_CullSetLayout)
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_CullSetLayout, nullptr);
        if (m_MeshCullSetLayout)
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_MeshCullSetLayout, nullptr);
    }

    void RendererImpl::AddLight(const LightData &light) {
        if (m_PendingLights.size() < GetMaxLights())
            m_PendingLights.push_back(light);
    }

    void RendererImpl::ClearLights() {
        m_PendingLights.clear();
    }

    void RendererImpl::OnResize(u32 width, u32 height) {
        if (m_PendingWidth == width && m_PendingHeight == height &&
            m_Swapchain.GetExtent().width == width && m_Swapchain.GetExtent().height == height) {
            return;
        }
        m_PendingWidth = width;
        m_PendingHeight = height;
        m_PendingResize = true;
    }

    void RendererImpl::SetSettings(const RenderSettings &settings) {
        RenderSettings normalized = settings;
        NormalizeRenderSettings(normalized, m_Context.GetMaxUsableSampleCount());

        bool needsResize = (m_Settings.resolutionScale != normalized.resolutionScale) ||
                           (m_Settings.msaaSamples != normalized.msaaSamples) ||
                           (m_Settings.enableVSync != normalized.enableVSync);
        bool needsShadowRecreate = (m_Settings.shadows.resolution != normalized.shadows.resolution);

        m_Settings = normalized;

        if (needsShadowRecreate) {
            m_PendingShadowRecreate = true;
        }

        if (needsResize) {
            m_PendingResize = true;
        }
    }

    Scope<MaterialInstance> RendererImpl::CreateMaterialInstance(const Ref<Material> &material) {
        return CreateScope<MaterialInstance>(material);
    }

    void RendererImpl::InitializeSwapchain(u32 width, u32 height, bool vsync) {
        m_Swapchain.Init(width, height, vsync);
    }

    void RendererImpl::RecreateSwapchain() {
        const u32 w = m_PendingWidth;
        const u32 h = m_PendingHeight;
        m_PendingResize = false;
        if (w == 0 || h == 0) return;

        m_Swapchain.Recreate(w, h, m_Settings.enableVSync);

        m_RenderTargets.Destroy();
        m_RenderExtent.width = std::max(1u, static_cast<u32>(w * m_Settings.resolutionScale));
        m_RenderExtent.height = std::max(1u, static_cast<u32>(h * m_Settings.resolutionScale));
        m_RenderTargets.Create(m_RenderExtent.width, m_RenderExtent.height,
                               ToVulkanSampleCount(m_Settings.msaaSamples));

        BuildPbrPipeline();
        BuildCompositePipeline();
        m_Skybox.RebuildPipeline(m_RenderTargets.GetOffscreenFormat(),
                                 m_RenderTargets.GetDepthFormat(),
                                 ToVulkanSampleCount(m_Settings.msaaSamples));
        if (m_DrawSystem) {
            m_DrawSystem->Shutdown();
            m_DrawSystem->Init(m_RenderTargets.GetOffscreenFormat(),
                               m_RenderTargets.GetDepthFormat(),
                               ToVulkanSampleCount(m_Settings.msaaSamples));
        }

        for (u32 i = 0; i < GetFrameCount(); ++i) {
            UpdateCompositeDescriptorSet(i);
            m_Shadow.UpdatePbrDescriptorSetShadow(m_Frames[i].pbrSet);
        }
        m_Swapchain.SetNeedsRecreate(false);
    }

    void RendererImpl::FinalizeFrameAndPresent(VkCommandBuffer cb) {
        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            b.dstAccessMask = 0;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            b.image = m_Swapchain.GetImage(m_CurrentImageIndex);
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
        m_Swapchain.SetImageLayout(m_CurrentImageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
            throw std::runtime_error("Failed to end frame command buffer");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSemaphores[] = {m_Swapchain.GetImageAvailableSemaphore(m_CurrentFrame)};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cb;
        VkSemaphore signalSemaphores[] = {m_Swapchain.GetRenderFinishedSemaphore(m_CurrentImageIndex)};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        VkFence submitFence = m_Swapchain.GetInFlightFence(m_CurrentFrame);
        if (vkQueueSubmit(m_Context.GetGraphicsQueue(), 1, &submitInfo, submitFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit frame command buffer");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapchains[] = {m_Swapchain.GetHandle()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &m_CurrentImageIndex;

        VkResult presentResult = vkQueuePresentKHR(m_Context.GetGraphicsQueue(), &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            m_Swapchain.SetNeedsRecreate(true);
        } else if (presentResult != VK_SUCCESS) {
            throw std::runtime_error("Failed to present swapchain image");
        }

        m_CurrentFrame = (m_CurrentFrame + 1) % GetFrameCount();
    }

    bool RendererImpl::BeginFrame() {
        // Handle recreate before acquiring a new swapchain image.
        // Acquiring first and then bailing out can leave frame fences unsignaled.
        if (m_PendingWidth == 0 || m_PendingHeight == 0) return false;
        if (m_PendingResize || m_Swapchain.NeedsRecreate()) {
            RecreateSwapchain();
            return false;
        }
        if (m_PendingShadowRecreate) {
            std::vector<VkDescriptorSet> pbrSets;
            pbrSets.reserve(m_Frames.size());
            for (auto &f: m_Frames) pbrSets.push_back(f.pbrSet);
            m_Shadow.Recreate(m_DescriptorPool, m_Settings.shadows, m_PbrSetLayout, pbrSets);
            m_PendingShadowRecreate = false;
            return false;
        }

        m_Textures.FlushPendingUploads();

        VkDevice device = m_Context.GetDevice();
        FrameData &frame = m_Frames[m_CurrentFrame];

        VkFence inFlightFence = m_Swapchain.GetInFlightFence(m_CurrentFrame);
        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

        VkResult acquireResult = vkAcquireNextImageKHR(device, m_Swapchain.GetHandle(), UINT64_MAX,
                                                       m_Swapchain.GetImageAvailableSemaphore(m_CurrentFrame),
                                                       VK_NULL_HANDLE, &m_CurrentImageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            m_Swapchain.SetNeedsRecreate(true);
            return false;
        }
        if (acquireResult == VK_SUBOPTIMAL_KHR) {
            m_Swapchain.SetNeedsRecreate(true);
        } else if (acquireResult != VK_SUCCESS) {
            return false;
        }

        vkResetFences(device, 1, &inFlightFence);
        vkResetCommandPool(device, frame.commandPool, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("Failed to begin frame command buffer");
        }

        m_PerFrameAlloc[m_CurrentFrame].Reset();

        m_LastFrameStats = m_CurrentFrameStats;
        m_CurrentFrameInstances.clear();
        m_CurrentFrameCullData.clear();

        m_CurrentFrameStats.Reset();
        m_CurrentFrameStats.drawCalls = static_cast<u32>(m_StaticInstances.size());
        m_CurrentFrameStats.instanceCount = static_cast<u32>(m_StaticInstances.size());
        m_CurrentFrameStats.triangleCount = m_StaticTriangleCount;

        if (m_DrawSystem) {
            m_DrawSystem->BeginFrame();
        }

        if (m_Overlay) m_Overlay->NewFrame();

        if (m_Overlay && m_Overlay->IsDebugUIEnabled()) {
            bool settingsChanged = false;
            RenderSettings editedSettings = m_Settings;
            m_Overlay->DrawDebugger(
                m_LastFrameStats.drawCalls,
                m_LastFrameStats.triangleCount,
                m_LastFrameStats.instanceCount,
                GetAdapterName(),
                editedSettings,
                settingsChanged
            );
            if (settingsChanged) {
                SetSettings(editedSettings);
            }
        }

        if (frame.commandBuffer == VK_NULL_HANDLE) {
            throw std::runtime_error("Command buffer is not available");
        }

        UploadLights(m_CurrentFrame);

        return true;
    }

    void RendererImpl::UploadLights(u32 frameIndex) {
        m_CurrentFrameStats.lightCount = static_cast<u32>(m_PendingLights.size());
        FrameData &frame = m_Frames[frameIndex];
        if (!m_PendingLights.empty())
            frame.lightBuffer->LoadData(
                m_PendingLights.data(),
                sizeof(LightData) * m_PendingLights.size());
    }

    void RendererImpl::BeginRendering() {
        m_Textures.FlushPendingUploads();

        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;
        VkExtent2D ext = m_RenderExtent;

        u32 staticInstCount = static_cast<u32>(m_StaticInstances.size());
        u32 dynamicInstCount = static_cast<u32>(m_CurrentFrameInstances.size());
        u32 totalInstCount = staticInstCount + dynamicInstCount;

        if (totalInstCount > 0) {
            if (!frame.staticUploaded && staticInstCount > 0) {
                frame.instanceBuffer->LoadData(m_StaticInstances.data(), sizeof(MeshInstance) * staticInstCount, 0);
                frame.cullDataBuffer->LoadData(m_StaticCullData.data(), sizeof(CullData) * staticInstCount, 0);
                frame.staticUploaded = true;
            }
            if (dynamicInstCount > 0) {
                frame.instanceBuffer->LoadData(m_CurrentFrameInstances.data(),
                                               sizeof(MeshInstance) * dynamicInstCount,
                                               sizeof(MeshInstance) * staticInstCount);
                frame.cullDataBuffer->LoadData(m_CurrentFrameCullData.data(), sizeof(CullData) * dynamicInstCount,
                                               sizeof(CullData) * staticInstCount);
            }

            vkCmdFillBuffer(cb, frame.countBuffer->GetHandle(), 0, sizeof(u32), 0);

            VkBufferMemoryBarrier2 fillBarrier{};
            fillBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            fillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            fillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            fillBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            fillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            fillBarrier.buffer = frame.countBuffer->GetHandle();
            fillBarrier.offset = 0;
            fillBarrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo fillDep{};
            fillDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            fillDep.bufferMemoryBarrierCount = 1;
            fillDep.pBufferMemoryBarriers = &fillBarrier;
            vkCmdPipelineBarrier2(cb, &fillDep);

            // Mesh culling
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_MeshCullPipeline->GetHandle());
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_MeshCullPipeline->GetLayout(), 0, 1, &frame.meshCullSet, 0, nullptr);

            MeshCullPushConstants mcpc{};
            Mat4 proj = m_ProjectionMatrix;
            proj[1][1] *= -1;
            Mat4 viewProj = proj * m_ViewMatrix;
            ExtractFrustumPlanes(viewProj, mcpc.planes);
            mcpc.instanceCount = totalInstCount;
            mcpc.cameraPos = Vec4(m_CameraPosition, 1.0f);
            mcpc.maxDrawDistance = m_Settings.maxDrawDistance;
            mcpc.enableFrustumCulling = m_Settings.enableFrustumCulling ? 1u : 0u;

            vkCmdPushConstants(cb, m_MeshCullPipeline->GetLayout(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(MeshCullPushConstants), &mcpc);

            u32 groupCount = (mcpc.instanceCount + 63) / 64;
            vkCmdDispatch(cb, groupCount, 1, 1);

            VkBufferMemoryBarrier2 meshCullBarriers[2]{};
            meshCullBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            meshCullBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            meshCullBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            meshCullBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            meshCullBarriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            meshCullBarriers[0].buffer = frame.indirectBuffer->GetHandle();
            meshCullBarriers[0].offset = 0;
            meshCullBarriers[0].size = VK_WHOLE_SIZE;
            meshCullBarriers[1] = meshCullBarriers[0];
            meshCullBarriers[1].buffer = frame.countBuffer->GetHandle();
            VkDependencyInfo meshDep{};
            meshDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            meshDep.bufferMemoryBarrierCount = 2;
            meshDep.pBufferMemoryBarriers = meshCullBarriers;
            vkCmdPipelineBarrier2(cb, &meshDep);

            if (m_Settings.shadows.enabled) {
                vkCmdFillBuffer(cb, frame.shadowCountBuffer->GetHandle(), 0, sizeof(u32), 0);

                VkBufferMemoryBarrier2 shadowFillBarrier{};
                shadowFillBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                shadowFillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                shadowFillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                shadowFillBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                shadowFillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                shadowFillBarrier.buffer = frame.shadowCountBuffer->GetHandle();
                shadowFillBarrier.offset = 0;
                shadowFillBarrier.size = VK_WHOLE_SIZE;
                VkDependencyInfo shadowFillDep{};
                shadowFillDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                shadowFillDep.bufferMemoryBarrierCount = 1;
                shadowFillDep.pBufferMemoryBarriers = &shadowFillBarrier;
                vkCmdPipelineBarrier2(cb, &shadowFillDep);

                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_MeshCullPipeline->GetHandle());
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        m_MeshCullPipeline->GetLayout(), 0, 1,
                                        &frame.shadowMeshCullSet, 0, nullptr);

                {
                    Vec3 lightDir = Vec3(m_Shadow.GetUniform().lightDir);
                    for (const auto &l: m_PendingLights)
                        if (l.type == shaderio::eLightTypeDirectional) {
                            lightDir = Vec3(l.direction.x, l.direction.y, l.direction.z);
                            break;
                        }
                    Mat4 shadowVP = ShadowSystem::ComputeLightViewProj(lightDir);

                    MeshCullPushConstants shadowPc{};
                    ExtractFrustumPlanes(shadowVP, shadowPc.planes);
                    shadowPc.instanceCount = totalInstCount;
                    shadowPc.cameraPos = Vec4(m_CameraPosition, 1.0f);
                    shadowPc.maxDrawDistance = m_Settings.maxDrawDistance;
                    shadowPc.enableFrustumCulling = m_Settings.enableFrustumCulling ? 1u : 0u;

                    vkCmdPushConstants(cb, m_MeshCullPipeline->GetLayout(),
                                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(shadowPc), &shadowPc);
                    vkCmdDispatch(cb, (totalInstCount + 63) / 64, 1, 1);
                }

                VkBufferMemoryBarrier2 shadowCullBarriers[2]{};
                shadowCullBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                shadowCullBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                shadowCullBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                shadowCullBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                shadowCullBarriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
                shadowCullBarriers[0].buffer = frame.shadowIndirectBuffer->GetHandle();
                shadowCullBarriers[0].offset = 0;
                shadowCullBarriers[0].size = VK_WHOLE_SIZE;
                shadowCullBarriers[1] = shadowCullBarriers[0];
                shadowCullBarriers[1].buffer = frame.shadowCountBuffer->GetHandle();
                VkDependencyInfo shadowCullDep{};
                shadowCullDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                shadowCullDep.bufferMemoryBarrierCount = 2;
                shadowCullDep.pBufferMemoryBarriers = shadowCullBarriers;
                vkCmdPipelineBarrier2(cb, &shadowCullDep);

                m_Shadow.RenderPass(cb,
                                    frame.pbrSet,
                                    frame.instanceBuffer->GetHandle(),
                                    totalInstCount,
                                    m_Meshes.GetIndexBuffer()->GetHandle(),
                                    m_Meshes.GetVertexBuffer()->GetHandle(),
                                    frame.shadowIndirectBuffer->GetHandle(),
                                    frame.shadowCountBuffer->GetHandle(),
                                    m_PendingLights,
                                    m_Settings.shadows);
            }

            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline->GetHandle());
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_CullPipeline->GetLayout(), 0, 1, &frame.cullSet, 0, nullptr);

            struct CullPushConstants {
                Mat4 view;
                Mat4 proj;
                Vec4 screenTile;
                u32 lightCount;
                u32 maxPerTile;
                u32 tilesX;
                u32 tilesY;
                Vec4 zParams;
            } cpc{};

            cpc.view = m_ViewMatrix;
            cpc.proj = m_ProjectionMatrix;
            cpc.proj[1][1] *= -1;
            const u32 tileSize = GetTileSize();
            cpc.screenTile = Vec4(static_cast<float>(ext.width), static_cast<float>(ext.height),
                                  static_cast<float>(tileSize), static_cast<float>(tileSize));
            cpc.lightCount = static_cast<u32>(m_PendingLights.size());
            cpc.maxPerTile = GetMaxLightsPerTile();
            cpc.tilesX = std::min((ext.width + tileSize - 1) / tileSize, GetMaxTilesX());
            cpc.tilesY = std::min((ext.height + tileSize - 1) / tileSize, GetMaxTilesY());
            cpc.zParams = Vec4(m_Settings.nearZ, m_Settings.farZ, 1.f, 0.f);

            vkCmdPushConstants(cb, m_CullPipeline->GetLayout(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cpc), &cpc);
            vkCmdDispatch(cb, cpc.tilesX, cpc.tilesY, 1);

            VkMemoryBarrier2 cullMemBarrier{};
            cullMemBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            cullMemBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            cullMemBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            cullMemBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            cullMemBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            VkDependencyInfo cullDep{};
            cullDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            cullDep.memoryBarrierCount = 1;
            cullDep.pMemoryBarriers = &cullMemBarrier;
            vkCmdPipelineBarrier2(cb, &cullDep);
        }


        {
            VkImageMemoryBarrier2 b[2]{};
            b[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b[0].srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[0].srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b[0].image = m_RenderTargets.GetOffscreenImage();
            b[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            u32 barrierCount = 1;
            if (m_RenderTargets.GetMsaaImage() != VK_NULL_HANDLE) {
                b[1] = b[0];
                b[1].image = m_RenderTargets.GetMsaaImage();
                b[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrierCount = 2;
            }

            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = barrierCount;
            dep.pImageMemoryBarriers = b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        UniformBufferObject ubo{};
        ubo.model = Mat4(1.f);
        ubo.view = m_ViewMatrix;
        ubo.proj = m_ProjectionMatrix;
        ubo.proj[1][1] *= -1;
        ubo.camPos = Vec4(m_CameraPosition, 1.f);
        ubo.exposure = m_Settings.postProcess.tonemapping.exposure;
        ubo.gamma = m_Settings.lighting.gamma;
        ubo.prefilteredCubeMipLevels = 1.f;
        ubo.scaleIBLAmbient = m_Settings.lighting.iblIntensity;
        ubo.lightCount = static_cast<int>(m_PendingLights.size());
        ubo.shadowsEnabled = m_Settings.shadows.enabled ? 1 : 0;
        ubo.aoIntensity = m_Settings.lighting.enableAmbientOcclusion ? m_Settings.lighting.aoIntensity : 0.0f;
        ubo.aoRadius = m_Settings.lighting.aoRadius;
        ubo.screenDimensions = Vec2(static_cast<float>(ext.width), static_cast<float>(ext.height));
        ubo.nearZ = m_Settings.nearZ;
        ubo.farZ = m_Settings.farZ;
        ubo.slicesZ = 1.f;
        ubo.reflectionEnabled = m_Settings.rayTracing.enableReflections ? 1 : 0;
        ubo.enableRayQueryReflections = m_Settings.rayTracing.enableReflections ? 1 : 0;
        ubo.enableRayQueryTransparency = m_Settings.rayTracing.enableTransparency ? 1 : 0;
        ubo.rayMaxBounces = m_Settings.rayTracing.maxBounces;
        ubo.materialCount = static_cast<int>(m_Materials.size());
        frame.uboBuffer->LoadData(&ubo, sizeof(ubo));

        if (m_MaterialsDirty && !m_Materials.empty()) {
            m_MaterialBuffer->LoadData(m_Materials.data(), sizeof(MaterialData) * m_Materials.size());
            m_MaterialsDirty = false;
        }

        {
            VkBufferMemoryBarrier2 b[5]{};
            b[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            b[0].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            b[0].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
            b[0].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b[0].buffer = m_MaterialBuffer->GetHandle();
            b[0].offset = 0;
            b[0].size = VK_WHOLE_SIZE;
            b[1] = b[0];
            b[1].buffer = m_TextureInfoBuffer->GetHandle();
            b[2] = b[0];
            b[2].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[2].buffer = frame.uboBuffer->GetHandle();
            b[3] = b[0];
            b[3].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b[3].buffer = frame.instanceBuffer->GetHandle();
            b[4] = b[0];
            b[4].buffer = frame.lightBuffer->GetHandle();
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = 5;
            dep.pBufferMemoryBarriers = b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
    }

    void RendererImpl::RenderQueue() {
        FrameData &frame = m_Frames[m_CurrentFrame];

        u32 instanceCount = static_cast<u32>(m_StaticInstances.size() + m_CurrentFrameInstances.size());
        auto *indexBuffer = m_Meshes.GetIndexBuffer();
        auto *vertexBuffer = m_Meshes.GetVertexBuffer();

        const bool hasMeshes = (instanceCount > 0 && m_ZPrepassPipeline && m_PbrPipeline && indexBuffer &&
                                vertexBuffer &&
                                m_RenderTargets.GetDepthView() != VK_NULL_HANDLE);
        const bool hasSkybox = m_Skybox.IsValid();

        Internal::ZPrepassPassState zState{};
        if (hasMeshes || m_RenderTargets.GetDepthView() != VK_NULL_HANDLE) {
            zState.extent = m_RenderExtent;
            zState.depthView = m_RenderTargets.GetDepthView();
            if (hasMeshes) {
                zState.pipeline = m_ZPrepassPipeline->GetHandle();
                zState.pipelineLayout = m_ZPrepassPipeline->GetLayout();
                zState.descriptorSets[0] = frame.pbrSet;
                zState.descriptorSets[1] = m_Textures.GetBindlessSet();
                zState.descriptorSetCount = 2;
                zState.indexBuffer = indexBuffer->GetHandle();
                zState.vertexBuffers[0] = vertexBuffer->GetHandle();
                zState.vertexBuffers[1] = frame.instanceBuffer->GetHandle();
                zState.indirectBuffer = frame.indirectBuffer->GetHandle();
                zState.countBuffer = frame.countBuffer->GetHandle();
                zState.instanceCount = instanceCount;
                zState.drawStride = sizeof(DrawCommand);
            }
            m_SceneRenderer->SetZPrepassState(&zState);
        }

        Internal::PbrPassState pbrState{};
        if (hasMeshes) {
            pbrState.extent = m_RenderExtent;
            pbrState.msaaSamples = ToVulkanSampleCount(m_Settings.msaaSamples);
            pbrState.msaaColorView = m_RenderTargets.GetMsaaView();
            pbrState.offscreenColorView = m_RenderTargets.GetOffscreenView();
            pbrState.depthView = m_RenderTargets.GetDepthView();
            pbrState.pipeline = m_PbrPipeline->GetHandle();
            pbrState.pipelineLayout = m_PbrPipeline->GetLayout();
            pbrState.descriptorSets[0] = frame.pbrSet;
            pbrState.descriptorSets[1] = m_Textures.GetBindlessSet();
            pbrState.descriptorSetCount = 2;
            pbrState.indexBuffer = indexBuffer->GetHandle();
            pbrState.vertexBuffers[0] = vertexBuffer->GetHandle();
            pbrState.vertexBuffers[1] = frame.instanceBuffer->GetHandle();
            pbrState.indirectBuffer = frame.indirectBuffer->GetHandle();
            pbrState.countBuffer = frame.countBuffer->GetHandle();
            pbrState.instanceCount = instanceCount;
            pbrState.drawStride = sizeof(DrawCommand);
            m_SceneRenderer->SetPbrPassState(&pbrState);
        }

        Internal::SkyboxPassState skyState{};
        if (hasSkybox) {
            skyState.extent = m_RenderExtent;
            skyState.offscreenColorView = m_RenderTargets.GetOffscreenView();
            skyState.msaaColorView = m_RenderTargets.GetMsaaView();
            skyState.msaaSamples = ToVulkanSampleCount(m_Settings.msaaSamples);
            skyState.depthView = m_RenderTargets.GetDepthView();
            skyState.pipeline = m_Skybox.GetPipeline();
            skyState.pipelineLayout = m_Skybox.GetPipelineLayout();
            skyState.descriptorSet = frame.skyboxSet;
            skyState.vertexBuffer = m_Skybox.GetVertexBuffer();
            skyState.indexBuffer = m_Skybox.GetIndexBuffer();
            skyState.indexCount = 36;
            m_SceneRenderer->SetSkyboxPassState(&skyState);
        }

        if (m_SceneRenderer) {
            m_SceneRenderer->Flush(frame.commandBuffer);
        }
    }


    void RendererImpl::EndRendering() {
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;

        if (m_DrawSystem) {
            m_DrawSystem->DispatchExpand(cb);

            Mat4 proj = m_ProjectionMatrix;
            proj[1][1] *= -1.0f;
            Mat4 viewProj = proj * m_ViewMatrix;

            const bool useMsaaResolve = (m_RenderTargets.GetMsaaView() != VK_NULL_HANDLE);
            VkImageView colorTarget = useMsaaResolve
                                          ? m_RenderTargets.GetMsaaView()
                                          : m_RenderTargets.GetOffscreenView();

            m_DrawSystem->Draw(cb, viewProj,
                               colorTarget,
                               m_RenderTargets.GetOffscreenView(),
                               m_RenderTargets.GetDepthView(),
                               m_RenderExtent.width, m_RenderExtent.height,
                               useMsaaResolve);
        }
    }

    void RendererImpl::EndFrameAndPresent() {
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;
        VkExtent2D ext{m_Swapchain.GetExtent().width, m_Swapchain.GetExtent().height};

        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = m_RenderTargets.GetOffscreenImage();
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = 0;
            b.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.oldLayout = m_Swapchain.GetImageLayout(m_CurrentImageIndex);
            b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.image = m_Swapchain.GetImage(m_CurrentImageIndex);
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
        m_Swapchain.SetImageLayout(m_CurrentImageIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        if (m_SceneRenderer) {
            CompositePushConstants cpc{};
            cpc.tm = m_Settings.postProcess.tonemapping;
            cpc.tm.inputMatrix = SlangFloat3x3(glm::mat3(cpc.tm.exposure));
            cpc.imageSize = Vec2(static_cast<float>(m_RenderExtent.width), static_cast<float>(m_RenderExtent.height));
            cpc.bloomIntensity = m_Settings.postProcess.bloomIntensity;
            cpc.bloomThreshold = m_Settings.postProcess.bloomThreshold;
            cpc.bloomEnabled = m_Settings.postProcess.enableBloom ? 1 : 0;

            Internal::CompositePassState compositeState{};
            compositeState.extent = ext;
            compositeState.colorView = m_Swapchain.GetImageView(m_CurrentImageIndex);
            compositeState.pipeline = m_CompositePipeline->GetHandle();
            compositeState.pipelineLayout = m_CompositePipeline->GetLayout();
            compositeState.descriptorSet = frame.compositeSet;
            compositeState.pushConstants = &cpc;
            compositeState.pushConstantSize = sizeof(CompositePushConstants);
            compositeState.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;

            m_SceneRenderer->SetCompositePassState(&compositeState);
            m_SceneRenderer->Flush(cb);
        }

        if (m_Overlay) {
            VkRenderingAttachmentInfo guiColorAtt{};
            guiColorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            guiColorAtt.imageView = m_Swapchain.GetImageView(m_CurrentImageIndex);
            guiColorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            guiColorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            guiColorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo guiRi{};
            guiRi.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            guiRi.renderArea.extent = ext;
            guiRi.layerCount = 1;
            guiRi.colorAttachmentCount = 1;
            guiRi.pColorAttachments = &guiColorAtt;
            vkCmdBeginRendering(cb, &guiRi);
            m_Overlay->Render(cb);
            vkCmdEndRendering(cb);
        }

        FinalizeFrameAndPresent(cb);
    }

    void RendererImpl::ComputeNormalMatrix(MeshInstance &inst, const Mat4 &model) {
        Vec3 s0 = Vec3(model[0]), s1 = Vec3(model[1]), s2 = Vec3(model[2]);
        float l0 = glm::dot(s0, s0), l1 = glm::dot(s1, s1), l2 = glm::dot(s2, s2);

        if (std::abs(l0 - 1.f) < 1e-4f && std::abs(l1 - 1.f) < 1e-4f && std::abs(l2 - 1.f) < 1e-4f) {
            for (int i = 0; i < 3; ++i) {
                inst.normalMatrix[i][0] = model[i][0];
                inst.normalMatrix[i][1] = model[i][1];
                inst.normalMatrix[i][2] = model[i][2];
                inst.normalMatrix[i][3] = 0.f;
            }
        } else if (std::abs(l0 - l1) < 1e-4f && std::abs(l0 - l2) < 1e-4f) {
            float invS2 = 1.f / l0;
            for (int i = 0; i < 3; ++i) {
                inst.normalMatrix[i][0] = model[i][0] * invS2;
                inst.normalMatrix[i][1] = model[i][1] * invS2;
                inst.normalMatrix[i][2] = model[i][2] * invS2;
                inst.normalMatrix[i][3] = 0.f;
            }
        } else {
            glm::mat3 n3 = glm::transpose(glm::inverse(glm::mat3(model)));
            for (int i = 0; i < 3; ++i) {
                inst.normalMatrix[i][0] = n3[i][0];
                inst.normalMatrix[i][1] = n3[i][1];
                inst.normalMatrix[i][2] = n3[i][2];
                inst.normalMatrix[i][3] = 0.f;
            }
        }
    }

    void RendererImpl::BuildCullData(CullData &out, const LoadedMesh *mesh, const Mat4 &model, u32 instanceId) {
        Vec3 s0 = Vec3(model[0]), s1 = Vec3(model[1]), s2 = Vec3(model[2]);
        float l0 = glm::dot(s0, s0), l1 = glm::dot(s1, s1), l2 = glm::dot(s2, s2);
        Vec3 worldCenter = Vec3(model * Vec4(mesh->center, 1.f));
        float scaleSq = std::max(l0, std::max(l1, l2));
        out.center[0] = worldCenter.x;
        out.center[1] = worldCenter.y;
        out.center[2] = worldCenter.z;
        out.radius = mesh->radius * std::sqrt(scaleSq);
        out.instanceId = instanceId;
    }

    void RendererImpl::ExtractFrustumPlanes(const Mat4 &viewProj, Vec4 planes[6]) {
        Mat4 m = glm::transpose(viewProj);
        Vec4 r0 = m[0], r1 = m[1], r2 = m[2], r3 = m[3];
        planes[0] = r3 + r0;
        planes[1] = r3 - r0;
        planes[2] = r3 + r1;
        planes[3] = r3 - r1;
        planes[4] = r3 + r2;
        planes[5] = r3 - r2;
        for (int i = 0; i < 6; ++i) {
            float len = glm::length(Vec3(planes[i]));
            planes[i] /= len;
        }
    }

    u32 RendererImpl::ResolveMaterialIndex(MaterialInstance &material) {
        u32 matIndex = material.GetRendererIndex();
        if (material.IsDirty() || matIndex == 0xFFFFFFFF) {
            const MaterialData &md = material.GetData();
            auto it = m_MaterialCache.find(md);
            if (it != m_MaterialCache.end()) {
                matIndex = it->second;
            } else {
                matIndex = static_cast<u32>(m_Materials.size());
                m_Materials.push_back(md);
                m_MaterialCache[md] = matIndex;
                m_MaterialsDirty = true;
            }
            material.SetRendererIndex(matIndex);
        }
        return matIndex;
    }

    void RendererImpl::DrawMesh(MeshHandle meshId, MaterialInstance &material, const Mat4 &model) {
        const auto *mesh = m_Meshes.Get(meshId);
        if (!mesh) return;

        u32 matIndex = ResolveMaterialIndex(material);

        m_CurrentFrameStats.drawCalls++;
        m_CurrentFrameStats.instanceCount++;
        m_CurrentFrameStats.triangleCount += mesh->indexCount / 3;

        MeshInstance inst{};
        inst.modelMatrix = model;
        ComputeNormalMatrix(inst, model);
        inst.firstVertex = mesh->firstVertex;
        inst.firstIndex = mesh->firstIndex;
        inst.indexCount = mesh->indexCount;
        inst.materialIndex = matIndex;
        inst.center[0] = mesh->center.x;
        inst.center[1] = mesh->center.y;
        inst.center[2] = mesh->center.z;
        inst.radius = mesh->radius;
        inst.flags = 0;

        CullData cullData{};
        BuildCullData(cullData, mesh, model, static_cast<u32>(m_CurrentFrameInstances.size()));

        m_CurrentFrameInstances.push_back(inst);
        m_CurrentFrameCullData.push_back(cullData);
    }

    void RendererImpl::DrawModel(const Model &model, const Mat4 &transform) {
        for (const auto &sm: model.GetSubMeshes())
            DrawMesh(sm.meshId, *sm.material, transform);
    }

    void RendererImpl::DrawModelStatic(const Model &model, const Mat4 &transform) {
        for (const auto &sm: model.GetSubMeshes())
            DrawMeshStatic(sm.meshId, *sm.material, transform);
    }

    void RendererImpl::DrawMeshStatic(MeshHandle meshId, MaterialInstance &material, const Mat4 &model) {
        const auto *mesh = m_Meshes.Get(meshId);
        if (!mesh) return;

        u32 matIndex = ResolveMaterialIndex(material);

        MeshInstance inst{};
        inst.modelMatrix = model;
        ComputeNormalMatrix(inst, model);
        inst.firstVertex = mesh->firstVertex;
        inst.firstIndex = mesh->firstIndex;
        inst.indexCount = mesh->indexCount;
        inst.materialIndex = matIndex;
        inst.center[0] = mesh->center.x;
        inst.center[1] = mesh->center.y;
        inst.center[2] = mesh->center.z;
        inst.radius = mesh->radius;
        inst.flags = 0;

        CullData cullData{};
        BuildCullData(cullData, mesh, model,
                      static_cast<u32>(m_StaticInstances.size() + m_CurrentFrameInstances.size()));

        m_StaticInstances.push_back(inst);
        m_StaticCullData.push_back(cullData);
        m_StaticTriangleCount += inst.indexCount / 3;
        for (auto &frame: m_Frames) {
            frame.staticUploaded = false;
        }
    }

    void RendererImpl::ClearStaticDraws() {
        m_StaticInstances.clear();
        m_StaticCullData.clear();
        m_StaticTriangleCount = 0;
        for (auto &frame: m_Frames) {
            frame.staticUploaded = false;
        }
    }

    void RendererImpl::CreateDescriptorLayouts() {
        {
            VkDescriptorSetLayoutBinding b[14];
            b[0] = {
                0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };
            b[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[2] = {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[3] = {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[4] = {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[5] = {
                9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };
            b[6] = {10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[7] = {11, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[8] = {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[9] = {13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[10] = {14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            b[11] = {
                15, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };
            b[12] = {
                16, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };
            b[13] = {
                17, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            };

            VkDescriptorBindingFlags flags[14];
            for (unsigned int &flag: flags) flag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

            VkDescriptorSetLayoutBindingFlagsCreateInfo bf{};
            bf.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bf.bindingCount = 14;
            bf.pBindingFlags = flags;

            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 14;
            ci.pBindings = b;
            ci.pNext = &bf;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_PbrSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create PBR descriptor set layout");
        }

        {
            VkDescriptorSetLayoutBinding b{
                0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
            };
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 1;
            ci.pBindings = &b;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_CompositeSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create composite descriptor set layout");
        }

        {
            VkDescriptorSetLayoutBinding b[4];
            b[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 4;
            ci.pBindings = b;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_CullSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create cull descriptor set layout");
        }

        {
            VkDescriptorSetLayoutBinding b[4];
            b[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            b[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 4;
            ci.pBindings = b;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_MeshCullSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create mesh cull descriptor set layout");
        }
    }

    void RendererImpl::CreateDescriptorPool() {
        const u32 frameCount = GetFrameCount();
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (frameCount * 10)},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (frameCount * 20)},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (frameCount * 10)},
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, (frameCount * 2)},
            {VK_DESCRIPTOR_TYPE_SAMPLER, (frameCount * 10)},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (frameCount * 10)},
        };
        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.poolSizeCount = 6;
        ci.pPoolSizes = sizes;
        ci.maxSets = frameCount * 24;
        if (vkCreateDescriptorPool(m_Context.GetDevice(), &ci, nullptr, &m_DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create descriptor pool");
    }

    void RendererImpl::CreateToGpuBuffers() {
        m_MaterialBuffer = CreateScope<Buffer>(
            m_Context, sizeof(MaterialData) * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_TextureInfoBuffer = CreateScope<Buffer>(
            m_Context, sizeof(shaderio::GltfTextureInfo) * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        std::vector<shaderio::GltfTextureInfo> tis(1024);
        for (int i = 0; i < 1024; ++i) {
            tis[i].uvTransform = Mat3x2(1.f);
            tis[i].index = i - 1;
            tis[i].texCoord = 0;
        }
        m_TextureInfoBuffer->LoadData(tis.data(), sizeof(shaderio::GltfTextureInfo) * 1024);
    }

    void RendererImpl::UpdatePbrDescriptorSet(u32 fi) {
        FrameData &frame = m_Frames[fi];

        VkDescriptorBufferInfo uboI{frame.uboBuffer->GetHandle(), 0, sizeof(UniformBufferObject)};
        VkDescriptorBufferInfo lightI{frame.lightBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo tileHI{frame.tileHeaderBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo tileLI{frame.tileLightIndexBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo instI{frame.instanceBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo matI{m_MaterialBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo texI{m_TextureInfoBuffer->GetHandle(), 0, VK_WHOLE_SIZE};

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = m_Textures.GetSampler();

        VkDescriptorImageInfo stubImg{};
        stubImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        stubImg.imageView = m_Textures.GetView(m_Textures.GetWhiteTextureId());

        VkWriteDescriptorSet writes[9]{};
        auto w = [&](int i, VkDescriptorSet set, u32 binding, VkDescriptorType type,
                     const VkDescriptorBufferInfo *bi = nullptr, const VkDescriptorImageInfo *ii = nullptr) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = binding;
            writes[i].descriptorType = type;
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = bi;
            writes[i].pImageInfo = ii;
        };
        w(0, frame.pbrSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &uboI);
        w(1, frame.pbrSet, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lightI);
        w(2, frame.pbrSet, 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tileHI);
        w(3, frame.pbrSet, 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tileLI);
        w(4, frame.pbrSet, 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instI);
        w(5, frame.pbrSet, 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &matI);
        w(6, frame.pbrSet, 1, VK_DESCRIPTOR_TYPE_SAMPLER, nullptr, &samplerInfo);
        w(7, frame.pbrSet, 10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &stubImg);
        w(8, frame.pbrSet, 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &texI);
        vkUpdateDescriptorSets(m_Context.GetDevice(), 9, writes, 0, nullptr);

        // Cull set
        VkWriteDescriptorSet cw[4]{};
        auto cull = [&](int i, u32 binding, VkDescriptorType type, const VkDescriptorBufferInfo *bi) {
            cw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cw[i].dstSet = frame.cullSet;
            cw[i].dstBinding = binding;
            cw[i].descriptorType = type;
            cw[i].descriptorCount = 1;
            cw[i].pBufferInfo = bi;
        };
        cull(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lightI);
        cull(1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tileHI);
        cull(2, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tileLI);
        cull(3, 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &uboI);
        vkUpdateDescriptorSets(m_Context.GetDevice(), 4, cw, 0, nullptr);

        // Mesh-cull set
        VkDescriptorBufferInfo cullDataI{frame.cullDataBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo indirectI{frame.indirectBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo countI{frame.countBuffer->GetHandle(), 0, VK_WHOLE_SIZE};

        VkWriteDescriptorSet mw[4]{};
        auto mc = [&](int i, u32 binding, const VkDescriptorBufferInfo *bi) {
            mw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mw[i].dstSet = frame.meshCullSet;
            mw[i].dstBinding = binding;
            mw[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            mw[i].descriptorCount = 1;
            mw[i].pBufferInfo = bi;
        };
        mc(0, 0, &cullDataI);
        mc(1, 1, &indirectI);
        mc(2, 2, &countI);
        mc(3, 3, &instI);
        vkUpdateDescriptorSets(m_Context.GetDevice(), 4, mw, 0, nullptr);

        VkDescriptorBufferInfo shadowIndirectI{frame.shadowIndirectBuffer->GetHandle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo shadowCountI{frame.shadowCountBuffer->GetHandle(), 0, VK_WHOLE_SIZE};

        VkWriteDescriptorSet sw[4]{};
        auto sc = [&](int i, u32 binding, const VkDescriptorBufferInfo *bi) {
            sw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            sw[i].dstSet = frame.shadowMeshCullSet;
            sw[i].dstBinding = binding;
            sw[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            sw[i].descriptorCount = 1;
            sw[i].pBufferInfo = bi;
        };
        sc(0, 0, &cullDataI);
        sc(1, 1, &shadowIndirectI);
        sc(2, 2, &shadowCountI);
        sc(3, 3, &instI);
        vkUpdateDescriptorSets(m_Context.GetDevice(), 4, sw, 0, nullptr);

        m_Shadow.UpdatePbrDescriptorSetShadow(frame.pbrSet);
        UpdateSkyboxDescriptorSet(fi);
    }

    void RendererImpl::UpdateCompositeDescriptorSet(u32 fi) {
        FrameData &frame = m_Frames[fi];

        VkDescriptorImageInfo imgI{};
        imgI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgI.imageView = m_RenderTargets.GetOffscreenView();
        imgI.sampler = m_RenderTargets.GetOffscreenSampler();

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = frame.compositeSet;
        w.dstBinding = 0;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo = &imgI;
        vkUpdateDescriptorSets(m_Context.GetDevice(), 1, &w, 0, nullptr);
    }

    void RendererImpl::UpdateSkyboxDescriptorSet(u32 fi) {
        if (m_Skybox.GetTexture() == kInvalidTexture) return;
        m_Skybox.UpdateDescriptorSet(fi, m_Frames[fi].skyboxSet,
                                     m_Frames[fi].uboBuffer->GetHandle(), m_Textures);
    }

    void RendererImpl::CreateCommandBuffers() {
        const u32 frameCount = GetFrameCount();
        const u32 maxLights = GetMaxLights();
        const u32 maxInstances = GetMaxInstances();
        const u32 maxTiles = GetMaxTiles();
        const u32 maxLightsPerTile = GetMaxLightsPerTile();

        m_Frames.resize(frameCount);

        for (u32 i = 0; i < frameCount; ++i) {
            FrameData &f = m_Frames[i];

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = m_Context.GetGraphicsQueueFamilyIndex();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            if (vkCreateCommandPool(m_Context.GetDevice(), &poolInfo, nullptr, &f.commandPool) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create frame command pool");
            }

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = f.commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(m_Context.GetDevice(), &allocInfo, &f.commandBuffer) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate frame command buffer");
            }

            f.uboBuffer = CreateScope<Buffer>(
                m_Context, sizeof(UniformBufferObject),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.lightBuffer = CreateScope<Buffer>(
                m_Context, sizeof(LightData) * maxLights,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            struct TileHeader {
                u32 offset, count, pad0, pad1;
            };
            f.tileHeaderBuffer = CreateScope<Buffer>(
                m_Context, sizeof(TileHeader) * maxTiles,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

            f.tileLightIndexBuffer = CreateScope<Buffer>(
                m_Context, sizeof(u32) * maxTiles * maxLightsPerTile,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

            f.instanceBuffer = CreateScope<Buffer>(
                m_Context, sizeof(MeshInstance) * maxInstances,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.cullDataBuffer = CreateScope<Buffer>(
                m_Context, sizeof(CullData) * maxInstances,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.indirectBuffer = CreateScope<Buffer>(
                m_Context, sizeof(DrawCommand) * maxInstances,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            f.countBuffer = CreateScope<Buffer>(
                m_Context, sizeof(u32),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            f.shadowIndirectBuffer = CreateScope<Buffer>(
                m_Context, sizeof(DrawCommand) * maxInstances,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            f.shadowCountBuffer = CreateScope<Buffer>(
                m_Context, sizeof(u32),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            VkDescriptorSetLayout layouts[5] = {
                m_PbrSetLayout, m_CompositeSetLayout,
                m_CullSetLayout, m_MeshCullSetLayout,
                m_Shadow.GetMeshCullSetLayout()
            };
            VkDescriptorSet sets[5];
            VkDescriptorSetAllocateInfo dsAI{};
            dsAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsAI.descriptorPool = m_DescriptorPool;
            dsAI.descriptorSetCount = 5;
            dsAI.pSetLayouts = layouts;
            if (vkAllocateDescriptorSets(m_Context.GetDevice(), &dsAI, sets) != VK_SUCCESS)
                throw std::runtime_error("Failed to allocate descriptor sets");
            f.pbrSet = sets[0];
            f.compositeSet = sets[1];
            f.cullSet = sets[2];
            f.meshCullSet = sets[3];
            f.shadowMeshCullSet = sets[4];

            VkDescriptorSetLayout skyLayout = m_Skybox.GetSetLayout();
            VkDescriptorSetAllocateInfo skyAI{};
            skyAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            skyAI.descriptorPool = m_DescriptorPool;
            skyAI.descriptorSetCount = 1;
            skyAI.pSetLayouts = &skyLayout;
            vkAllocateDescriptorSets(m_Context.GetDevice(), &skyAI, &f.skyboxSet);


            UpdateCompositeDescriptorSet(i);
            UpdatePbrDescriptorSet(i);
        }
    }

    void RendererImpl::BuildPbrPipeline() {
        auto vertSpv = VirtualFS::Get().ReadFile("shaders://pbr.vert.spv");
        auto fragSpv = VirtualFS::Get().ReadFile("shaders://pbr.frag.spv");
        auto zPrepassFragSpv = VirtualFS::Get().ReadFile("shaders://pbr_zprepass.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] PBR shaders not found");
            return;
        }
        if (zPrepassFragSpv.empty()) {
            LOG_WARN("[Renderer] Alpha-cutout z-prepass shader not found, masked materials may render opaque.");
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.colorAttachmentFormat = m_RenderTargets.GetOffscreenFormat();
        cfg.depthAttachmentFormat = m_RenderTargets.GetDepthFormat();
        cfg.msaaSamples = ToVulkanSampleCount(m_Settings.msaaSamples);
        cfg.pushConstantSize = sizeof(PBRPushConstants);
        cfg.descriptorSetLayouts = {m_PbrSetLayout, m_Textures.GetBindlessLayout()};

        cfg.vertexInputBindings.resize(2);
        cfg.vertexInputBindings[0] = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        cfg.vertexInputBindings[1] = {1, sizeof(MeshInstance), VK_VERTEX_INPUT_RATE_INSTANCE};

        cfg.vertexInputAttributes.resize(12);
        cfg.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<u32>(offsetof(Vertex, position))};
        cfg.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<u32>(offsetof(Vertex, normal))};
        cfg.vertexInputAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<u32>(offsetof(Vertex, uv))};
        cfg.vertexInputAttributes[3] = {
            3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(Vertex, tangent))
        };
        cfg.vertexInputAttributes[4] = {
            4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            static_cast<u32>(offsetof(MeshInstance, modelMatrix))
        };
        cfg.vertexInputAttributes[5] = {
            5, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            static_cast<u32>(offsetof(MeshInstance, modelMatrix)) + 16
        };
        cfg.vertexInputAttributes[6] = {
            6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(MeshInstance, modelMatrix)) + 32
        };
        cfg.vertexInputAttributes[7] = {
            7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(MeshInstance, modelMatrix)) + 48
        };
        cfg.vertexInputAttributes[8] = {
            8, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            static_cast<u32>(offsetof(MeshInstance, normalMatrix))
        };
        cfg.vertexInputAttributes[9] = {
            9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(MeshInstance, normalMatrix)) + 16
        };
        cfg.vertexInputAttributes[10] = {
            10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<u32>(offsetof(MeshInstance, normalMatrix)) + 32
        };
        cfg.vertexInputAttributes[11] = {
            11, 1, VK_FORMAT_R32_UINT, static_cast<u32>(offsetof(MeshInstance, materialIndex))
        };

        cfg.depthWriteEnable = VK_FALSE;
        cfg.depthCompareOp = VK_COMPARE_OP_EQUAL;

        m_PbrPipeline = CreateScope<Pipeline>(m_Context);
        m_PbrPipeline->BuildGraphics(vertSpv, fragSpv, cfg);

        PipelineConfigParams zCfg = cfg;
        zCfg.fragmentEntryPoint = zPrepassFragSpv.empty() ? "" : "main";
        zCfg.colorAttachmentFormat = VK_FORMAT_UNDEFINED;
        zCfg.depthWriteEnable = VK_TRUE;
        zCfg.depthCompareOp = VK_COMPARE_OP_LESS;

        m_ZPrepassPipeline = CreateScope<Pipeline>(m_Context);
        m_ZPrepassPipeline->BuildGraphics(vertSpv, zPrepassFragSpv, zCfg);

        VkDescriptorSetLayoutBinding stub{
            0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
        };
        VkDescriptorSetLayoutCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dci.bindingCount = 1;
        dci.pBindings = &stub;
        VkDescriptorSetLayout stubLayout;
        vkCreateDescriptorSetLayout(m_Context.GetDevice(), &dci, nullptr, &stubLayout);
        m_DefaultMaterial = CreateRef<Material>(m_Context, nullptr, stubLayout);
    }

    void RendererImpl::BuildCompositePipeline() {
        auto vertSpv = VirtualFS::Get().ReadFile("shaders://composite.vert.spv");
        auto fragSpv = VirtualFS::Get().ReadFile("shaders://composite.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] Composite shaders not found");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.colorAttachmentFormat = m_Swapchain.GetFormat();
        cfg.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        cfg.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        cfg.pushConstantSize = sizeof(CompositePushConstants);
        cfg.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;
        cfg.descriptorSetLayouts = {m_CompositeSetLayout};

        m_CompositePipeline = CreateScope<Pipeline>(m_Context);
        m_CompositePipeline->BuildGraphics(vertSpv, fragSpv, cfg);
    }

    void RendererImpl::BuildCullPipeline() {
        {
            auto compSpv = VirtualFS::Get().ReadFile("shaders://forward_plus_cull.comp.spv");
            if (compSpv.empty()) {
                LOG_ERROR("[Renderer] Cull shader not found");
                return;
            }

            PipelineConfigParams cfg{};
            cfg.computeEntryPoint = "main";
            cfg.pushConstantSize = 176;
            cfg.descriptorSetLayouts = {m_CullSetLayout};

            PipelineKey key{};
            key.compHash = PipelineCache::HashSpirV(compSpv);
            key.variants = PipelineVariant_Compute;
            key.pushConstantSize = 176;
            VkDescriptorSetLayout layouts[] = {m_CullSetLayout};
            key.setLayoutHash = PipelineCache::HashLayouts(layouts, 1);

            m_CullPipeline = CreateScope<Pipeline>(m_Context);
            m_PipelineCache.GetCompute(key, [&](VkPipelineCache) -> VkPipeline {
                m_CullPipeline->BuildCompute(compSpv, cfg);
                return m_CullPipeline->GetHandle();
            });
        }

        {
            auto compSpv = VirtualFS::Get().ReadFile("shaders://mesh_cull.comp.spv");
            if (compSpv.empty()) {
                LOG_ERROR("[Renderer] Mesh cull shader not found");
                return;
            }

            PipelineConfigParams cfg{};
            cfg.computeEntryPoint = "main";
            cfg.pushConstantSize = sizeof(MeshCullPushConstants);
            cfg.descriptorSetLayouts = {m_MeshCullSetLayout};

            PipelineKey key{};
            key.compHash = PipelineCache::HashSpirV(compSpv);
            key.variants = PipelineVariant_Compute;
            key.pushConstantSize = sizeof(MeshCullPushConstants);
            VkDescriptorSetLayout layouts[] = {m_MeshCullSetLayout};
            key.setLayoutHash = PipelineCache::HashLayouts(layouts, 1);

            m_MeshCullPipeline = CreateScope<Pipeline>(m_Context);
            m_PipelineCache.GetCompute(key, [&](VkPipelineCache) -> VkPipeline {
                m_MeshCullPipeline->BuildCompute(compSpv, cfg);
                return m_MeshCullPipeline->GetHandle();
            });
        }
    }

    void RendererImpl::DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) const {
        if (m_DrawSystem) {
            m_DrawSystem->SubmitLine(a, b, color, depthTest);
        }
    }

    void RendererImpl::DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) const {
        if (m_DrawSystem) {
            m_DrawSystem->SubmitBox((min + max) * 0.5f, (max - min) * 0.5f, Mat4(1.0f), color, depthTest);
        }
    }

    void RendererImpl::DrawBox(const Vec3 &center, const Vec3 &half,
                               const Mat4 &transform, u32 color, bool depthTest) const {
        if (m_DrawSystem) {
            m_DrawSystem->SubmitBox(center, half, transform, color, depthTest);
        }
    }

    void RendererImpl::DrawSphere(const Vec3 &center, float radius,
                                  u32 color, int segments, bool depthTest) const {
        if (m_DrawSystem) {
            m_DrawSystem->SubmitSphere(center, radius, color, segments, depthTest);
        }
    }

    void RendererImpl::DrawFrustum(const Mat4 &invViewProj, u32 color, bool depthTest) const {
        if (m_DrawSystem) {
            m_DrawSystem->SubmitFrustum(invViewProj, color, depthTest);
        }
    }

    void RendererImpl::DrawCross(const Vec3 &center, float size, u32 color, bool depthTest) const {
        if (m_DrawSystem) {
            m_DrawSystem->SubmitCross(center, size, color, depthTest);
        }
    }

    void RendererImpl::DrawAxes(const Mat4 &transform, float size) const {
        Vec3 origin = Vec3(transform[3]);
        Vec3 axisX = Vec3(transform[0]) * size;
        Vec3 axisY = Vec3(transform[1]) * size;
        Vec3 axisZ = Vec3(transform[2]) * size;

        DrawLine(origin, origin + axisX, 0xFF0000FFu, true);
        DrawLine(origin, origin + axisY, 0xFF00FF00u, true);
        DrawLine(origin, origin + axisZ, 0xFFFF0000u, true);
    }

    Scope<RendererImpl> CreateRendererImpl(IWindow &window, u32 width, u32 height,
                                           const RenderSettings &settings, const RendererConfig &config) {
        return CreateScope<RendererImpl>(window, width, height, settings, config);
    }

    Renderer::Renderer(IWindow &window, u32 width, u32 height, const RenderSettings &settings)
        : Renderer(window, width, height, settings, RendererConfig::Default()) {
    }

    Renderer::Renderer(IWindow &window, u32 width, u32 height, const RenderSettings &settings,
                       const RendererConfig &config)
        : m_Impl(CreateRendererImpl(window, width, height, settings, config)) {
    }

    Renderer::~Renderer() = default;

    bool RendererImplBeginFrame(RendererImpl &impl) { return impl.BeginFrame(); }
    void RendererImplBeginRendering(RendererImpl &impl) { impl.BeginRendering(); }
    void RendererImplRenderQueue(RendererImpl &impl) { impl.RenderQueue(); }
    void RendererImplEndRendering(RendererImpl &impl) { impl.EndRendering(); }
    void RendererImplEndFrameAndPresent(RendererImpl &impl) { impl.EndFrameAndPresent(); }

    void RendererImplDrawMesh(RendererImpl &impl, MeshHandle mesh, MaterialInstance &mat, const Mat4 &model) {
        impl.DrawMesh(mesh, mat, model);
    }

    void RendererImplDrawMeshStatic(RendererImpl &impl, MeshHandle mesh, MaterialInstance &mat, const Mat4 &model) {
        impl.DrawMeshStatic(mesh, mat, model);
    }

    void RendererImplClearStaticDraws(RendererImpl &impl) { impl.ClearStaticDraws(); }

    void RendererImplDrawModel(RendererImpl &impl, const Model &model, const Mat4 &transform) {
        impl.DrawModel(model, transform);
    }

    void RendererImplDrawModelStatic(RendererImpl &impl, const Model &model, const Mat4 &transform) {
        impl.DrawModelStatic(model, transform);
    }

    void RendererImplAddLight(RendererImpl &impl, const LightData &light) { impl.AddLight(light); }
    void RendererImplClearLights(RendererImpl &impl) { impl.ClearLights(); }

    void RendererImplSetViewProjection(RendererImpl &impl, const Mat4 &view, const Mat4 &proj) {
        impl.SetViewProjection(view, proj);
    }

    void RendererImplSetCameraPosition(RendererImpl &impl, const Vec3 &pos) { impl.SetCameraPosition(pos); }
    void RendererImplSetSkybox(RendererImpl &impl, TextureHandle cubemap) { impl.SetSkybox(cubemap); }
    MeshHandle RendererImplUploadMesh(RendererImpl &impl, const ModelData &data) { return impl.UploadMesh(data); }

    TextureHandle RendererImplUploadTexture(RendererImpl &impl, const TextureData &data) {
        return impl.UploadTexture(data);
    }

    TextureHandle RendererImplUploadCubemap(RendererImpl &impl, const std::vector<TextureData> &faces) {
        return impl.UploadCubemap(faces);
    }

    Ref<Material> RendererImplGetDefaultMaterial(const RendererImpl &impl) { return impl.GetDefaultMaterial(); }

    Scope<MaterialInstance> RendererImplCreateMaterialInstance(RendererImpl &impl, const Ref<Material> &mat) {
        return RendererImpl::CreateMaterialInstance(mat);
    }

    void RendererImplOnResize(RendererImpl &impl, u32 width, u32 height) { impl.OnResize(width, height); }
    float RendererImplGetAspectRatio(const RendererImpl &impl) { return impl.GetAspectRatio(); }
    MeshManager &RendererImplGetMeshes(RendererImpl &impl) { return impl.GetMeshes(); }
    void RendererImplSetSettings(RendererImpl &impl, const RenderSettings &settings) { impl.SetSettings(settings); }
    const RenderSettings &RendererImplGetSettingsConst(const RendererImpl &impl) { return impl.GetSettings(); }
    RenderSettings &RendererImplGetSettings(RendererImpl &impl) { return impl.GetSettings(); }
    const RendererConfig &RendererImplGetConfig(const RendererImpl &impl) { return impl.GetConfig(); }

    const FrameStats &RendererImplGetLastFrameStats(const RendererImpl &impl) { return impl.GetLastFrameStats(); }
    void RendererImplSetDebugUIEnabled(const RendererImpl &impl, bool enabled) { impl.SetDebugUIEnabled(enabled); }
    bool RendererImplIsDebugUIEnabled(const RendererImpl &impl) { return impl.IsDebugUIEnabled(); }

    void RendererImplGetVramStats(const RendererImpl &impl, u64 &usage, u64 &budget) {
        impl.GetVramStats(usage, budget);
    }

    std::string RendererImplGetAdapterName(const RendererImpl &impl) { return impl.GetAdapterName(); }

    void RendererImplDrawLine(const RendererImpl &impl, const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) {
        impl.DrawLine(a, b, color, depthTest);
    }

    void RendererImplDrawAABB(const RendererImpl &impl, const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) {
        impl.DrawAABB(min, max, color, depthTest);
    }

    void RendererImplDrawBox(const RendererImpl &impl, const Vec3 &center, const Vec3 &half, const Mat4 &transform,
                             u32 color, bool depthTest) {
        impl.DrawBox(center, half, transform, color, depthTest);
    }

    void RendererImplDrawSphere(const RendererImpl &impl, const Vec3 &center, float radius, u32 color, int segments,
                                bool depthTest) {
        impl.DrawSphere(center, radius, color, segments, depthTest);
    }

    void RendererImplDrawFrustum(const RendererImpl &impl, const Mat4 &invViewProj, u32 color, bool depthTest) {
        impl.DrawFrustum(invViewProj, color, depthTest);
    }

    void RendererImplDrawCross(const RendererImpl &impl, const Vec3 &center, float size, u32 color, bool depthTest) {
        impl.DrawCross(center, size, color, depthTest);
    }

    void RendererImplDrawAxes(const RendererImpl &impl, const Mat4 &transform, float size) {
        impl.DrawAxes(transform, size);
    }
} // namespace Manro
