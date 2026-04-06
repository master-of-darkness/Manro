#include "Internal/RendererInternal.h"
#include "Backend/Vulkan/VulkanHelpers.h"
#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Render/Model.h>
#include <Manro/Render/SceneRenderer.h>
#include <VkBootstrap.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>


#include "Backend/Vulkan/VulkanContext.h"
#include "Backend/Vulkan/Buffer.h"
#include "Backend/Vulkan/Pipeline.h"
#include "Backend/Vulkan/DescriptorAllocator.h"
#include "Backend/Vulkan/PipelineCache.h"
#include "Internal/ScenePassState.h"
#include "Manro/Render/DebugDraw.h"

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
        Scope<Buffer> debugVertexBuffer;
        Scope<Buffer> debugVertexBufferNoDepth;
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

    struct ShadowUniformData {
        Mat4 lightViewProj;
        Vec4 lightDir;
        Vec2 shadowMapSize;
        float normalBias;
        float softShadows;
        int shadowsEnabled;
        float _pad[3];
    };

    struct ShadowPushConstants {
        Mat4 lightViewProj;
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
            m_SkyboxTexture = cubemap;
            for (u32 i = 0; i < GetFrameCount(); ++i) {
                UpdateSkyboxDescriptorSet(i);
            }
        }

        MeshHandle UploadMesh(const ModelData &data) { return m_Meshes.Upload(data); }

        TextureHandle UploadTexture(const TextureData &data) { return m_Textures.Upload(data); }

        TextureHandle UploadCubemap(const std::vector<TextureData> &faces) { return m_Textures.UploadCubemap(faces); }

        Ref<Material> GetDefaultMaterial() const { return m_DefaultMaterial; }


        Scope<MaterialInstance> CreateMaterialInstance(Ref<Material> mat);

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

        void SetDebugUIEnabled(bool enabled) {
            if (m_ImGuiLayer) m_ImGuiLayer->SetDebugUIEnabled(enabled);
        }

        bool IsDebugUIEnabled() const {
            return m_ImGuiLayer && m_ImGuiLayer->IsDebugUIEnabled();
        }

        void GetVramStats(u64 &usage, u64 &budget) const {
            m_Context.GetVramStats(usage, budget);
        }

        std::string GetAdapterName() const {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(m_Context.GetPhysicalDevice(), &props);
            return props.deviceName;
        }

        void DebugLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest);

        void DebugAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest);

        void DebugBox(const Vec3 &center, const Vec3 &half, const Mat4 &transform, u32 color, bool depthTest);

        void DebugSphere(const Vec3 &center, float radius, u32 color, int segments, bool depthTest);

        void DebugFrustum(const Mat4 &invViewProj, u32 color, bool depthTest);

        void DebugCross(const Vec3 &center, float size, u32 color, bool depthTest);

        void DebugAxes(const Mat4 &transform, float size);

    private:
        void CreateOffscreenResources(u32 w, u32 h);

        void CreateDepthResources(u32 w, u32 h);

        void CreateColorResources(u32 w, u32 h);

        void CreateShadowResources();

        void RecreateShadowResources();

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

        void InitializeSwapchain(u32 width, u32 height, bool vsync);

        void CleanupSwapchain();

        void RecreateSwapchain();

        void UploadLights(u32 frameIndex);

        void RenderShadowPass(VkCommandBuffer cb);

        Mat4 ComputeLightViewProj(const Vec3 &lightDir) const;

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

        Scope<Overlay> m_ImGuiLayer;
        Ref<Material> m_DefaultMaterial;

        std::vector<MeshInstance> m_StaticInstances;
        std::vector<CullData> m_StaticCullData;
        u32 m_StaticTriangleCount = 0;
        std::vector<DebugVertex> m_DebugVertices;
        std::vector<DebugVertex> m_DebugVerticesNoDepth;

        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_PbrSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CompositeSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_MeshCullSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_ShadowMeshCullSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_SkyboxSetLayout = VK_NULL_HANDLE;

        Scope<Pipeline> m_PbrPipeline;
        Scope<Pipeline> m_ZPrepassPipeline;
        Scope<Pipeline> m_CompositePipeline;
        Scope<Pipeline> m_CullPipeline;
        Scope<Pipeline> m_MeshCullPipeline;
        Scope<Pipeline> m_ShadowPipeline;
        Scope<Pipeline> m_SkyboxPipeline;
        Scope<Pipeline> m_DebugPipeline;
        Scope<Pipeline> m_DebugPipelineNoDepth;

        Scope<Buffer> m_SkyboxVertexBuffer;
        Scope<Buffer> m_SkyboxIndexBuffer;
        TextureHandle m_SkyboxTexture = kInvalidTexture;

        AllocatedImage m_OffscreenColor{};
        AllocatedImage m_MsaaColorImage{};
        AllocatedImage m_DepthImage{};
        AllocatedImage m_ShadowMap{};

        VkSampler m_OffscreenSampler = VK_NULL_HANDLE;
        VkSampler m_ShadowSampler = VK_NULL_HANDLE;

        VkFormat m_OffscreenFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat m_DepthFormat = VK_FORMAT_D32_SFLOAT;

        std::vector<FrameData> m_Frames;
        u32 m_CurrentFrame = 0;
        u32 m_CurrentImageIndex = 0;
        VkSwapchainKHR m_Swapchain{VK_NULL_HANDLE};
        VkFormat m_SwapchainFormat{VK_FORMAT_UNDEFINED};
        VkExtent2D m_SwapchainExtent{};
        std::vector<VkImage> m_SwapchainImages;
        std::vector<VkImageView> m_SwapchainImageViews;
        std::vector<VkImageLayout> m_SwapchainImageLayouts;
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;
        bool m_SwapchainNeedsRecreate{false};

        std::vector<MaterialData> m_Materials;
        std::unordered_map<MaterialData, u32, MaterialDataHash> m_MaterialCache;
        Scope<Buffer> m_MaterialBuffer;
        Scope<Buffer> m_TextureInfoBuffer;
        bool m_MaterialsDirty = true;

        Scope<Buffer> m_ShadowUniformBuffer;
        ShadowUniformData m_ShadowUniform{};

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

        void BuildSkyboxPipeline();

        void UpdateSkyboxDescriptorSet(u32 fi);

        void BuildDebugPipeline();

        void FlushDebugDraw(VkCommandBuffer cb);
    };

    static constexpr u32 kMaxDebugVertices = 65536;

    static void NormalizeRenderSettings(RenderSettings &settings, VkSampleCountFlagBits maxSamples) {
        settings.resolutionScale = std::clamp(settings.resolutionScale, 0.1f, 2.0f);

        if (settings.aaMode != AntiAliasingMode::MSAA) {
            settings.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        } else if (static_cast<u32>(settings.msaaSamples) > static_cast<u32>(maxSamples)) {
            settings.msaaSamples = maxSamples;
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
          m_Textures(m_Context, m_BindlessAlloc), m_Meshes(m_Context), m_Settings(settings),
          m_Config(config) {
        NormalizeRenderSettings(m_Settings, m_Context.GetMaxUsableSampleCount());
        InitializeSwapchain(width, height, m_Settings.enableVSync);

        m_PendingWidth = width;
        m_PendingHeight = height;
        m_RenderExtent.width = std::max(1u, (u32) (width * m_Settings.resolutionScale));
        m_RenderExtent.height = std::max(1u, (u32) (height * m_Settings.resolutionScale));

        VkDevice device = m_Context.GetDevice();

        m_PerFrameAlloc.resize(GetFrameCount());
        for (auto &alloc: m_PerFrameAlloc)
            alloc.Init(device, 256);

        m_PersistentAlloc.Init(device, 64);
        m_BindlessAlloc.Init(device);
        m_Textures.InitDefaults();
        m_PipelineCache.Init(device, "manro_pipeline_cache.bin");
        m_SceneRenderer = CreateScope<SceneRenderer>();

        CreateOffscreenResources(m_RenderExtent.width, m_RenderExtent.height);
        CreateColorResources(m_RenderExtent.width, m_RenderExtent.height);
        CreateDepthResources(m_RenderExtent.width, m_RenderExtent.height);
        CreateShadowResources();
        CreateDescriptorLayouts();
        CreateDescriptorPool();
        CreateGpuBuffers();
        BuildPbrPipeline();
        BuildCompositePipeline();
        BuildCullPipeline();
        BuildShadowPipeline();
        BuildSkyboxPipeline();
        BuildDebugPipeline();
        CreateCommandBuffers();
        CreateSyncObjects();

        m_CurrentFrameInstances.reserve(GetMaxInstances());
        m_CurrentFrameCullData.reserve(GetMaxInstances());
        m_PendingLights.reserve(GetMaxLights());

        m_Materials.push_back(shaderio::defaultGltfMaterial());
        m_MaterialBuffer->LoadData(m_Materials.data(), sizeof(MaterialData));

        OverlayInfo guiInfo{};
        guiInfo.context = &m_Context;
        guiInfo.window = &window;
        guiInfo.colorFormat = m_SwapchainFormat;
        guiInfo.imageCount = static_cast<u32>(m_SwapchainImages.size());
        m_ImGuiLayer = CreateScope<Overlay>(guiInfo);
    }

    RendererImpl::~RendererImpl() {
        if (!m_Context.GetDevice()) return;
        vkDeviceWaitIdle(m_Context.GetDevice());

        m_PipelineCache.Shutdown();
        m_BindlessAlloc.Shutdown();
        m_PersistentAlloc.Shutdown();
        for (auto &alloc: m_PerFrameAlloc)
            alloc.Shutdown();

        m_ImGuiLayer.reset();
        m_DefaultMaterial.reset();
        m_PbrPipeline.reset();
        m_ZPrepassPipeline.reset();
        m_CompositePipeline.reset();
        m_CullPipeline.reset();
        m_MeshCullPipeline.reset();
        m_ShadowPipeline.reset();

        if (m_OffscreenSampler)
            vkDestroySampler(m_Context.GetDevice(), m_OffscreenSampler, nullptr);
        if (m_ShadowSampler)
            vkDestroySampler(m_Context.GetDevice(), m_ShadowSampler, nullptr);

        DestroyImage(m_Context, m_OffscreenColor);
        DestroyImage(m_Context, m_MsaaColorImage);
        DestroyImage(m_Context, m_DepthImage);
        DestroyImage(m_Context, m_ShadowMap);

        for (auto fence: m_InFlightFences) {
            if (fence != VK_NULL_HANDLE)
                vkDestroyFence(m_Context.GetDevice(), fence, nullptr);
        }
        for (auto sem: m_ImageAvailableSemaphores) {
            if (sem != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Context.GetDevice(), sem, nullptr);
        }
        for (auto sem: m_RenderFinishedSemaphores) {
            if (sem != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Context.GetDevice(), sem, nullptr);
        }

        for (auto &f: m_Frames)
            if (f.commandPool)
                vkDestroyCommandPool(m_Context.GetDevice(), f.commandPool, nullptr);

        CleanupSwapchain();

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
        if (m_ShadowMeshCullSetLayout)
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_ShadowMeshCullSetLayout, nullptr);
        if (m_SkyboxSetLayout)
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_SkyboxSetLayout, nullptr);
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
            m_SwapchainExtent.width == width && m_SwapchainExtent.height == height) {
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

    Scope<MaterialInstance> RendererImpl::CreateMaterialInstance(Ref<Material> material) {
        return CreateScope<MaterialInstance>(material);
    }

    void RendererImpl::InitializeSwapchain(u32 width, u32 height, bool vsync) {
        vkb::SwapchainBuilder swapchainBuilder{
            m_Context.GetPhysicalDevice(),
            m_Context.GetDevice(),
            m_Context.GetSurface()
        };

        if (vsync) {
            swapchainBuilder
                    .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
        } else {
            swapchainBuilder
                    .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
                    .add_fallback_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
                    .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
        }

        auto vkbSwapchainRet = swapchainBuilder
                .use_default_format_selection()
                .set_desired_extent(width, height)
                .add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build();

        if (!vkbSwapchainRet) {
            throw std::runtime_error("Failed to create Vulkan swapchain");
        }

        vkb::Swapchain vkbSwapchain = vkbSwapchainRet.value();
        m_Swapchain = vkbSwapchain.swapchain;
        m_SwapchainExtent = vkbSwapchain.extent;
        m_SwapchainFormat = vkbSwapchain.image_format;

        auto imagesRet = vkbSwapchain.get_images();
        auto imageViewsRet = vkbSwapchain.get_image_views();
        if (!imagesRet || !imageViewsRet) {
            throw std::runtime_error("Failed to get swapchain images/views");
        }

        m_SwapchainImages = imagesRet.value();
        m_SwapchainImageViews = imageViewsRet.value();
        m_SwapchainImageLayouts.assign(m_SwapchainImages.size(), VK_IMAGE_LAYOUT_UNDEFINED);
        m_SwapchainNeedsRecreate = false;
    }

    void RendererImpl::CleanupSwapchain() {
        VkDevice device = m_Context.GetDevice();
        for (auto view: m_SwapchainImageViews) {
            if (view != VK_NULL_HANDLE)
                vkDestroyImageView(device, view, nullptr);
        }
        m_SwapchainImageViews.clear();
        m_SwapchainImages.clear();
        m_SwapchainImageLayouts.clear();
        if (m_Swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
    }

    void RendererImpl::RecreateSwapchain() {
        const u32 w = m_PendingWidth;
        const u32 h = m_PendingHeight;
        m_PendingResize = false;
        if (w == 0 || h == 0) return;

        vkDeviceWaitIdle(m_Context.GetDevice());

        for (auto sem: m_RenderFinishedSemaphores) {
            if (sem != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Context.GetDevice(), sem, nullptr);
        }
        m_RenderFinishedSemaphores.clear();

        CleanupSwapchain();
        InitializeSwapchain(w, h, m_Settings.enableVSync);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        m_RenderFinishedSemaphores.resize(m_SwapchainImages.size());
        for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); ++i) {
            if (vkCreateSemaphore(m_Context.GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) !=
                VK_SUCCESS) {
                throw std::runtime_error("Failed to recreate render-finished semaphores");
            }
        }

        if (m_OffscreenSampler) {
            vkDestroySampler(m_Context.GetDevice(), m_OffscreenSampler, nullptr);
            m_OffscreenSampler = VK_NULL_HANDLE;
        }
        DestroyImage(m_Context, m_OffscreenColor);
        DestroyImage(m_Context, m_MsaaColorImage);
        DestroyImage(m_Context, m_DepthImage);

        m_RenderExtent.width = std::max(1u, (u32) (w * m_Settings.resolutionScale));
        m_RenderExtent.height = std::max(1u, (u32) (h * m_Settings.resolutionScale));

        CreateOffscreenResources(m_RenderExtent.width, m_RenderExtent.height);
        CreateColorResources(m_RenderExtent.width, m_RenderExtent.height);
        CreateDepthResources(m_RenderExtent.width, m_RenderExtent.height);

        BuildPbrPipeline();
        BuildCompositePipeline();
        BuildSkyboxPipeline();
        BuildDebugPipeline();

        for (u32 i = 0; i < GetFrameCount(); ++i) {
            UpdateCompositeDescriptorSet(i);
            UpdatePbrDescriptorSetShadow(i);
        }
        m_SwapchainNeedsRecreate = false;
    }

    void RendererImpl::CreateOffscreenResources(u32 width, u32 height) {
        ImageCreateParams p{};
        p.width = width;
        p.height = height;
        p.format = m_OffscreenFormat;
        p.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        p.samples = VK_SAMPLE_COUNT_1_BIT;
        m_OffscreenColor = CreateImage(m_Context, p);

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = VK_LOD_CLAMP_NONE;
        if (vkCreateSampler(m_Context.GetDevice(), &si, nullptr, &m_OffscreenSampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create offscreen sampler");

        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_OffscreenColor.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        });
    }

    void RendererImpl::CreateDepthResources(u32 width, u32 height) {
        ImageCreateParams p{};
        p.width = width;
        p.height = height;
        p.format = m_DepthFormat;
        p.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        p.samples = m_Settings.msaaSamples;
        m_DepthImage = CreateImage(m_Context, p, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    void RendererImpl::CreateColorResources(u32 width, u32 height) {
        if (m_Settings.msaaSamples == VK_SAMPLE_COUNT_1_BIT) return;
        ImageCreateParams p{};
        p.width = width;
        p.height = height;
        p.format = m_OffscreenFormat;
        p.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        p.samples = m_Settings.msaaSamples;
        m_MsaaColorImage = CreateImage(m_Context, p);
    }

    void RendererImpl::CreateShadowResources() {
        const u32 shadowMapSize = static_cast<u32>(std::max(128, m_Settings.shadows.resolution));
        {
            ImageCreateParams p{};
            p.width = shadowMapSize;
            p.height = shadowMapSize;
            p.format = VK_FORMAT_D32_SFLOAT;
            p.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            p.samples = VK_SAMPLE_COUNT_1_BIT;
            m_ShadowMap = CreateImage(m_Context, p, VK_IMAGE_ASPECT_DEPTH_BIT);
        }

        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_ShadowMap.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        });

        {
            VkSamplerCreateInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            si.magFilter = VK_FILTER_LINEAR;
            si.minFilter = VK_FILTER_LINEAR;
            si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            si.compareEnable = VK_TRUE;
            si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            si.maxLod = 0.f;
            if (vkCreateSampler(m_Context.GetDevice(), &si, nullptr, &m_ShadowSampler) != VK_SUCCESS)
                throw std::runtime_error("Failed to create shadow sampler");
        }

        m_ShadowUniformBuffer = CreateScope<Buffer>(
            m_Context,
            sizeof(ShadowUniformData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_ShadowUniform.lightDir = Vec4(0.5f, -0.7f, 0.5f, 0.005f);
        m_ShadowUniform.shadowMapSize = Vec2(shadowMapSize, shadowMapSize);
        m_ShadowUniform.normalBias = m_Settings.shadows.bias;
        m_ShadowUniform.softShadows = m_Settings.shadows.softShadows;
        m_ShadowUniform.shadowsEnabled = m_Settings.shadows.enabled ? 1 : 0;
    }

    void RendererImpl::RecreateShadowResources() {
        vkDeviceWaitIdle(m_Context.GetDevice());

        if (m_ShadowSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_Context.GetDevice(), m_ShadowSampler, nullptr);
            m_ShadowSampler = VK_NULL_HANDLE;
        }
        DestroyImage(m_Context, m_ShadowMap);
        m_ShadowUniformBuffer.reset();

        CreateShadowResources();

        for (u32 i = 0; i < GetFrameCount(); ++i) {
            UpdatePbrDescriptorSetShadow(i);
        }
    }

    Mat4 RendererImpl::ComputeLightViewProj(const Vec3 &lightDir) const {
        const float worldRadius = 3500.f;
        const float depth = 10000.f;

        Vec3 normDir = glm::normalize(lightDir);
        Vec3 target = Vec3(0.f, 200.f, 0.f);
        Vec3 lightPos = target - normDir * (depth * 0.5f);
        Vec3 up = (std::abs(normDir.y) > 0.99f)
                      ? Vec3(0.f, 0.f, 1.f)
                      : Vec3(0.f, 1.f, 0.f);

        Mat4 view = glm::lookAt(lightPos, target, up);
        Mat4 proj = glm::ortho(-worldRadius, worldRadius,
                               -worldRadius, worldRadius,
                               0.f, depth);
        proj[1][1] *= -1.f;
        return proj * view;
    }

    void RendererImpl::RenderShadowPass(VkCommandBuffer cb) {
        if (!m_Settings.shadows.enabled) return;

        const u32 shadowMapSize = static_cast<u32>(std::max(128, m_Settings.shadows.resolution));
        u32 totalInstCount = static_cast<u32>(m_StaticInstances.size() + m_CurrentFrameInstances.size());
        if (!m_ShadowPipeline || totalInstCount == 0) return;

        FrameData &frame = m_Frames[m_CurrentFrame];

        Vec3 lightDir = Vec3(m_ShadowUniform.lightDir);
        for (const auto &l: m_PendingLights)
            if (l.type == shaderio::eLightTypeDirectional) {
                lightDir = Vec3(l.direction.x, l.direction.y, l.direction.z);
                break;
            }
        m_ShadowUniform.lightViewProj = ComputeLightViewProj(lightDir);
        m_ShadowUniform.lightDir = Vec4(lightDir, m_Settings.shadows.bias);
        m_ShadowUniform.normalBias = m_Settings.shadows.bias;
        m_ShadowUniform.softShadows = m_Settings.shadows.softShadows;
        m_ShadowUniform.shadowsEnabled = m_Settings.shadows.enabled ? 1 : 0;
        m_ShadowUniformBuffer->LoadData(&m_ShadowUniform, sizeof(ShadowUniformData));

        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            b.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_ShadowMap.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
        VkRenderingAttachmentInfo depthAtt{};
        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAtt.imageView = m_ShadowMap.view;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.clearValue.depthStencil = {1.f, 0};

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent = {shadowMapSize, shadowMapSize};
        ri.layerCount = 1;
        ri.pDepthAttachment = &depthAtt;
        vkCmdBeginRendering(cb, &ri);

        VkViewport vp{0.f, 0.f, float(shadowMapSize), float(shadowMapSize), 0.f, 1.f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor{
            {0, 0},
            {shadowMapSize, shadowMapSize}
        };
        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdSetDepthBias(cb, m_Settings.shadows.bias, 0.0f, m_Settings.shadows.slopeBias);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline->GetHandle());
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_ShadowPipeline->GetLayout(), 0, 1,
                                &frame.pbrSet, 0, nullptr);

        ShadowPushConstants pc{};
        pc.lightViewProj = m_ShadowUniform.lightViewProj;
        vkCmdPushConstants(cb, m_ShadowPipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        VkBuffer vbufs[2] = {
            m_Meshes.GetVertexBuffer()->GetHandle(),
            frame.instanceBuffer->GetHandle()
        };
        VkDeviceSize offsets[2] = {0, 0};
        vkCmdBindVertexBuffers(cb, 0, 2, vbufs, offsets);
        vkCmdBindIndexBuffer(cb, m_Meshes.GetIndexBuffer()->GetHandle(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexedIndirectCount(cb,
                                      frame.shadowIndirectBuffer->GetHandle(), 0,
                                      frame.shadowCountBuffer->GetHandle(), 0,
                                      totalInstCount, sizeof(DrawCommand));

        vkCmdEndRendering(cb);

        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            b.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_ShadowMap.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
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
            b.image = m_SwapchainImages[m_CurrentImageIndex];
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
        m_SwapchainImageLayouts[m_CurrentImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
            throw std::runtime_error("Failed to end frame command buffer");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_CurrentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cb;
        VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_CurrentImageIndex]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(m_Context.GetGraphicsQueue(), 1, &submitInfo, m_InFlightFences[m_CurrentFrame]) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to submit frame command buffer");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapchains[] = {m_Swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &m_CurrentImageIndex;

        VkResult presentResult = vkQueuePresentKHR(m_Context.GetGraphicsQueue(), &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            m_SwapchainNeedsRecreate = true;
        } else if (presentResult != VK_SUCCESS) {
            throw std::runtime_error("Failed to present swapchain image");
        }

        m_CurrentFrame = (m_CurrentFrame + 1) % GetFrameCount();
    }

    bool RendererImpl::BeginFrame() {
        // Handle recreate before acquiring a new swapchain image.
        // Acquiring first and then bailing out can leave frame fences unsignaled.
        if (m_PendingWidth == 0 || m_PendingHeight == 0) return false;
        if (m_PendingResize || m_SwapchainNeedsRecreate) {
            RecreateSwapchain();
            return false;
        }
        if (m_PendingShadowRecreate) {
            RecreateShadowResources();
            m_PendingShadowRecreate = false;
            return false;
        }

        m_Textures.FlushPendingUploads();

        VkDevice device = m_Context.GetDevice();
        FrameData &frame = m_Frames[m_CurrentFrame];

        vkWaitForFences(device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        VkResult acquireResult = vkAcquireNextImageKHR(device, m_Swapchain, UINT64_MAX,
                                                       m_ImageAvailableSemaphores[m_CurrentFrame],
                                                       VK_NULL_HANDLE, &m_CurrentImageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            m_SwapchainNeedsRecreate = true;
            return false;
        }
        if (acquireResult == VK_SUBOPTIMAL_KHR) {
            m_SwapchainNeedsRecreate = true;
        } else if (acquireResult != VK_SUCCESS) {
            return false;
        }

        vkResetFences(device, 1, &m_InFlightFences[m_CurrentFrame]);
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
        m_CurrentFrameStats.drawCalls = (u32) m_StaticInstances.size();
        m_CurrentFrameStats.instanceCount = (u32) m_StaticInstances.size();
        m_CurrentFrameStats.triangleCount = m_StaticTriangleCount;

        if (m_ImGuiLayer) m_ImGuiLayer->NewFrame();

        if (m_ImGuiLayer && m_ImGuiLayer->IsDebugUIEnabled()) {
            bool settingsChanged = false;
            RenderSettings editedSettings = m_Settings;
            m_ImGuiLayer->DrawDebugUI(
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

        u32 staticInstCount = (u32) m_StaticInstances.size();
        u32 dynamicInstCount = (u32) m_CurrentFrameInstances.size();
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
            Mat4 m = glm::transpose(viewProj);
            Vec4 r0 = m[0], r1 = m[1], r2 = m[2], r3 = m[3];
            mcpc.planes[0] = r3 + r0;
            mcpc.planes[1] = r3 - r0;
            mcpc.planes[2] = r3 + r1;
            mcpc.planes[3] = r3 - r1;
            mcpc.planes[4] = r3 + r2;
            mcpc.planes[5] = r3 - r2;
            for (int i = 0; i < 6; ++i) {
                float len = glm::length(Vec3(mcpc.planes[i]));
                mcpc.planes[i] /= len;
            }
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
                    Vec3 lightDir = Vec3(m_ShadowUniform.lightDir);
                    for (const auto &l: m_PendingLights)
                        if (l.type == shaderio::eLightTypeDirectional) {
                            lightDir = Vec3(l.direction.x, l.direction.y, l.direction.z);
                            break;
                        }
                    Mat4 shadowVP = ComputeLightViewProj(lightDir);

                    MeshCullPushConstants shadowPc{};
                    m = glm::transpose(shadowVP);

                    r0 = m[0];
                    r1 = m[1];
                    r2 = m[2];
                    r3 = m[3];

                    shadowPc.planes[0] = r3 + r0;
                    shadowPc.planes[1] = r3 - r0;
                    shadowPc.planes[2] = r3 + r1;
                    shadowPc.planes[3] = r3 - r1;
                    shadowPc.planes[4] = r3 + r2;
                    shadowPc.planes[5] = r3 - r2;
                    for (int i = 0; i < 6; ++i) {
                        float len = glm::length(Vec3(shadowPc.planes[i]));
                        shadowPc.planes[i] /= len;
                    }
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

                RenderShadowPass(cb);
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
            } cpc;

            cpc.view = m_ViewMatrix;
            cpc.proj = m_ProjectionMatrix;
            cpc.proj[1][1] *= -1;
            const u32 tileSize = GetTileSize();
            cpc.screenTile = Vec4((float) ext.width, (float) ext.height,
                                  (float) tileSize, (float) tileSize);
            cpc.lightCount = (u32) m_PendingLights.size();
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
            b[0].image = m_OffscreenColor.image;
            b[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            u32 barrierCount = 1;
            if (m_MsaaColorImage.image != VK_NULL_HANDLE) {
                b[1] = b[0];
                b[1].image = m_MsaaColorImage.image;
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
        ubo.lightCount = (int) m_PendingLights.size();
        ubo.shadowsEnabled = m_Settings.shadows.enabled ? 1 : 0;
        ubo.aoIntensity = m_Settings.lighting.enableAmbientOcclusion ? m_Settings.lighting.aoIntensity : 0.0f;
        ubo.aoRadius = m_Settings.lighting.aoRadius;
        ubo.screenDimensions = Vec2((float) ext.width, (float) ext.height);
        ubo.nearZ = m_Settings.nearZ;
        ubo.farZ = m_Settings.farZ;
        ubo.slicesZ = 1.f;
        ubo.reflectionEnabled = m_Settings.rayTracing.enableReflections ? 1 : 0;
        ubo.enableRayQueryReflections = m_Settings.rayTracing.enableReflections ? 1 : 0;
        ubo.enableRayQueryTransparency = m_Settings.rayTracing.enableTransparency ? 1 : 0;
        ubo.rayMaxBounces = m_Settings.rayTracing.maxBounces;
        ubo.materialCount = (int) m_Materials.size();
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

        u32 instanceCount = (u32) (m_StaticInstances.size() + m_CurrentFrameInstances.size());
        auto *indexBuffer = m_Meshes.GetIndexBuffer();
        auto *vertexBuffer = m_Meshes.GetVertexBuffer();

        const bool hasMeshes = (instanceCount > 0 && m_ZPrepassPipeline && m_PbrPipeline && indexBuffer &&
                                vertexBuffer &&
                                m_DepthImage.image != VK_NULL_HANDLE);
        const bool hasSkybox = (m_SkyboxTexture != kInvalidTexture && m_SkyboxPipeline);

        Internal::ZPrepassPassState zState{};
        if (hasMeshes || m_DepthImage.image != VK_NULL_HANDLE) {
            zState.extent = m_RenderExtent;
            zState.depthView = m_DepthImage.view;
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
            pbrState.msaaSamples = m_Settings.msaaSamples;
            pbrState.msaaColorView = m_MsaaColorImage.view;
            pbrState.offscreenColorView = m_OffscreenColor.view;
            pbrState.depthView = m_DepthImage.view;
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
            skyState.offscreenColorView = m_OffscreenColor.view;
            skyState.msaaColorView = m_MsaaColorImage.view;
            skyState.msaaSamples = m_Settings.msaaSamples;
            skyState.depthView = m_DepthImage.view;
            skyState.pipeline = m_SkyboxPipeline->GetHandle();
            skyState.pipelineLayout = m_SkyboxPipeline->GetLayout();
            skyState.descriptorSet = frame.skyboxSet;
            skyState.vertexBuffer = m_SkyboxVertexBuffer->GetHandle();
            skyState.indexBuffer = m_SkyboxIndexBuffer->GetHandle();
            skyState.indexCount = 36;
            m_SceneRenderer->SetSkyboxPassState(&skyState);
        }

        if (m_SceneRenderer) {
            m_SceneRenderer->Flush(frame.commandBuffer);
        }
    }


    void RendererImpl::EndRendering() {
        FlushDebugDraw(m_Frames[m_CurrentFrame].commandBuffer);
    }

    void RendererImpl::EndFrameAndPresent() {
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;
        VkExtent2D ext{m_SwapchainExtent.width, m_SwapchainExtent.height};

        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = m_OffscreenColor.image;
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
            b.oldLayout = m_SwapchainImageLayouts[m_CurrentImageIndex];
            b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.image = m_SwapchainImages[m_CurrentImageIndex];
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
        m_SwapchainImageLayouts[m_CurrentImageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        if (m_SceneRenderer) {
            CompositePushConstants cpc{};
            cpc.tm = m_Settings.postProcess.tonemapping;
            cpc.tm.inputMatrix = SlangFloat3x3(glm::mat3(cpc.tm.exposure));
            cpc.imageSize = Vec2((float) m_RenderExtent.width, (float) m_RenderExtent.height);
            cpc.bloomIntensity = m_Settings.postProcess.bloomIntensity;
            cpc.bloomThreshold = m_Settings.postProcess.bloomThreshold;
            cpc.bloomEnabled = m_Settings.postProcess.enableBloom ? 1 : 0;

            Internal::CompositePassState compositeState{};
            compositeState.extent = ext;
            compositeState.colorView = m_SwapchainImageViews[m_CurrentImageIndex];
            compositeState.pipeline = m_CompositePipeline->GetHandle();
            compositeState.pipelineLayout = m_CompositePipeline->GetLayout();
            compositeState.descriptorSet = frame.compositeSet;
            compositeState.pushConstants = &cpc;
            compositeState.pushConstantSize = sizeof(CompositePushConstants);
            compositeState.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;

            m_SceneRenderer->SetCompositePassState(&compositeState);
            m_SceneRenderer->Flush(cb);
        }

        if (m_ImGuiLayer) {
            VkRenderingAttachmentInfo guiColorAtt{};
            guiColorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            guiColorAtt.imageView = m_SwapchainImageViews[m_CurrentImageIndex];
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
            m_ImGuiLayer->Render(cb);
            vkCmdEndRendering(cb);
        }

        FinalizeFrameAndPresent(cb);
    }

    void RendererImpl::DrawMesh(MeshHandle meshId, MaterialInstance &material, const Mat4 &model) {
        const auto *mesh = m_Meshes.Get(meshId);
        if (!mesh) return;

        u32 matIndex = material.GetRendererIndex();
        if (material.IsDirty() || matIndex == 0xFFFFFFFF) {
            const MaterialData &md = material.GetData();
            auto it = m_MaterialCache.find(md);
            if (it != m_MaterialCache.end()) {
                matIndex = it->second;
            } else {
                matIndex = (u32) m_Materials.size();
                m_Materials.push_back(md);
                m_MaterialCache[md] = matIndex;
                m_MaterialsDirty = true;
            }
            material.SetRendererIndex(matIndex);
        }

        m_CurrentFrameStats.drawCalls++;
        m_CurrentFrameStats.instanceCount++;
        m_CurrentFrameStats.triangleCount += mesh->indexCount / 3;

        MeshInstance inst{};
        inst.modelMatrix = model;

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
        Vec3 worldCenter = Vec3(model * Vec4(mesh->center, 1.f));
        float scaleSq = std::max(l0, std::max(l1, l2));
        cullData.center[0] = worldCenter.x;
        cullData.center[1] = worldCenter.y;
        cullData.center[2] = worldCenter.z;
        cullData.radius = mesh->radius * std::sqrt(scaleSq);
        cullData.instanceId = (u32) m_CurrentFrameInstances.size();

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

        u32 matIndex = material.GetRendererIndex();
        if (material.IsDirty() || matIndex == 0xFFFFFFFF) {
            const MaterialData &md = material.GetData();
            auto it = m_MaterialCache.find(md);
            if (it != m_MaterialCache.end()) {
                matIndex = it->second;
            } else {
                matIndex = (u32) m_Materials.size();
                m_Materials.push_back(md);
                m_MaterialCache[md] = matIndex;
                m_MaterialsDirty = true;
            }
            material.SetRendererIndex(matIndex);
        }


        MeshInstance inst{};
        inst.modelMatrix = model;

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
        Vec3 worldCenter = Vec3(model * Vec4(mesh->center, 1.f));
        float scaleSq = std::max(l0, std::max(l1, l2));
        cullData.center[0] = worldCenter.x;
        cullData.center[1] = worldCenter.y;
        cullData.center[2] = worldCenter.z;
        cullData.radius = mesh->radius * std::sqrt(scaleSq);
        cullData.instanceId = (u32) (m_StaticInstances.size() + m_CurrentFrameInstances.size());

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
            for (int i = 0; i < 14; ++i) flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

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
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_ShadowMeshCullSetLayout) !=
                VK_SUCCESS)
                throw std::runtime_error("Failed to create shadow mesh cull descriptor set layout");
        }

        {
            VkDescriptorSetLayoutBinding b[2];
            b[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
            b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = 2;
            ci.pBindings = b;
            if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &ci, nullptr, &m_SkyboxSetLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create skybox descriptor set layout");
        }
    }

    void RendererImpl::CreateDescriptorPool() {
        const u32 frameCount = GetFrameCount();
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, u32(frameCount * 10)},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, u32(frameCount * 20)},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, u32(frameCount * 10)},
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, u32(frameCount * 2)},
            {VK_DESCRIPTOR_TYPE_SAMPLER, u32(frameCount * 10)},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, u32(frameCount * 10)},
        };
        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.poolSizeCount = 6;
        ci.pPoolSizes = sizes;
        ci.maxSets = u32(frameCount * 24);
        if (vkCreateDescriptorPool(m_Context.GetDevice(), &ci, nullptr, &m_DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create descriptor pool");
    }

    void RendererImpl::CreateGpuBuffers() {
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

        float skyboxVertices[] = {
            -5000.0f, 5000.0f, -5000.0f, -5000.0f, -5000.0f, -5000.0f, 5000.0f, -5000.0f, -5000.0f,
            5000.0f, -5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, -5000.0f, 5000.0f, -5000.0f,
            -5000.0f, -5000.0f, 5000.0f, -5000.0f, -5000.0f, -5000.0f, -5000.0f, 5000.0f, -5000.0f,
            -5000.0f, 5000.0f, -5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, -5000.0f, 5000.0f,
            5000.0f, -5000.0f, -5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, 5000.0f, 5000.0f,
            5000.0f, 5000.0f, 5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, -5000.0f, -5000.0f,
            -5000.0f, -5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, 5000.0f, 5000.0f, 5000.0f,
            5000.0f, 5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, -5000.0f, -5000.0f, 5000.0f,
            -5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, 5000.0f,
            5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f, -5000.0f,
            -5000.0f, -5000.0f, -5000.0f, -5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, -5000.0f,
            5000.0f, -5000.0f, -5000.0f, -5000.0f, -5000.0f, 5000.0f, 5000.0f, -5000.0f, 5000.0f
        };
        u32 skyboxIndices[36];
        for (u32 i = 0; i < 36; ++i) skyboxIndices[i] = i;

        m_SkyboxVertexBuffer = CreateScope<Buffer>(m_Context, sizeof(skyboxVertices),
                                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_SkyboxVertexBuffer->LoadData(skyboxVertices, sizeof(skyboxVertices));

        m_SkyboxIndexBuffer = CreateScope<Buffer>(m_Context, sizeof(skyboxIndices),
                                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_SkyboxIndexBuffer->LoadData(skyboxIndices, sizeof(skyboxIndices));

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
                     VkDescriptorBufferInfo *bi = nullptr, VkDescriptorImageInfo *ii = nullptr) {
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
        auto cull = [&](int i, u32 binding, VkDescriptorType type, VkDescriptorBufferInfo *bi) {
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
        auto mc = [&](int i, u32 binding, VkDescriptorBufferInfo *bi) {
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
        auto sc = [&](int i, u32 binding, VkDescriptorBufferInfo *bi) {
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

        UpdatePbrDescriptorSetShadow(fi);
        UpdateSkyboxDescriptorSet(fi);
    }

    void RendererImpl::UpdatePbrDescriptorSetShadow(u32 fi) {
        FrameData &frame = m_Frames[fi];

        VkDescriptorImageInfo shadowImgI{};
        shadowImgI.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowImgI.imageView = m_ShadowMap.view;

        VkDescriptorImageInfo shadowSamplerI{};
        shadowSamplerI.sampler = m_ShadowSampler;

        VkDescriptorBufferInfo shadowUboI{m_ShadowUniformBuffer->GetHandle(), 0, sizeof(ShadowUniformData)};

        VkWriteDescriptorSet writes[3]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frame.pbrSet;
        writes[0].dstBinding = 15;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &shadowImgI;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = frame.pbrSet;
        writes[1].dstBinding = 16;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &shadowSamplerI;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = frame.pbrSet;
        writes[2].dstBinding = 17;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &shadowUboI;

        vkUpdateDescriptorSets(m_Context.GetDevice(), 3, writes, 0, nullptr);
    }

    void RendererImpl::UpdateCompositeDescriptorSet(u32 fi) {
        FrameData &frame = m_Frames[fi];

        VkDescriptorImageInfo imgI{};
        imgI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgI.imageView = m_OffscreenColor.view;
        imgI.sampler = m_OffscreenSampler;

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
        if (m_SkyboxTexture == kInvalidTexture) return;
        FrameData &frame = m_Frames[fi];

        VkDescriptorBufferInfo uboI{frame.uboBuffer->GetHandle(), 0, sizeof(UniformBufferObject)};
        VkDescriptorImageInfo skyI{};
        skyI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        skyI.imageView = m_Textures.GetView(m_SkyboxTexture);
        skyI.sampler = m_Textures.GetSampler();

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frame.skyboxSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &uboI;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = frame.skyboxSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &skyI;

        vkUpdateDescriptorSets(m_Context.GetDevice(), 2, writes, 0, nullptr);
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

            f.debugVertexBuffer = CreateScope<Buffer>(
                m_Context,
                sizeof(DebugVertex) * kMaxDebugVertices,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.debugVertexBufferNoDepth = CreateScope<Buffer>(
                m_Context,
                sizeof(DebugVertex) * kMaxDebugVertices,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

            VkDescriptorSetLayout layouts[5] = {
                m_PbrSetLayout, m_CompositeSetLayout,
                m_CullSetLayout, m_MeshCullSetLayout,
                m_ShadowMeshCullSetLayout
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

            VkDescriptorSetAllocateInfo skyAI{};
            skyAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            skyAI.descriptorPool = m_DescriptorPool;
            skyAI.descriptorSetCount = 1;
            skyAI.pSetLayouts = &m_SkyboxSetLayout;
            vkAllocateDescriptorSets(m_Context.GetDevice(), &skyAI, &f.skyboxSet);


            UpdateCompositeDescriptorSet(i);
            UpdatePbrDescriptorSet(i);
        }
    }

    void RendererImpl::CreateSyncObjects() {
        const u32 frameCount = GetFrameCount();
        m_ImageAvailableSemaphores.resize(frameCount);
        m_InFlightFences.resize(frameCount);
        m_RenderFinishedSemaphores.resize(m_SwapchainImages.size());

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (u32 i = 0; i < frameCount; ++i) {
            if (vkCreateSemaphore(m_Context.GetDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) !=
                VK_SUCCESS) {
                throw std::runtime_error("Failed to create image-available semaphore");
            }
            if (vkCreateFence(m_Context.GetDevice(), &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create in-flight fence");
            }
        }

        for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); ++i) {
            if (vkCreateSemaphore(m_Context.GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) !=
                VK_SUCCESS) {
                throw std::runtime_error("Failed to create render-finished semaphore");
            }
        }
    }

    void RendererImpl::BuildPbrPipeline() {
        auto vertSpv = VirtualFS::Get().ReadFile("shaders://pbr.vert.spv");
        auto fragSpv = VirtualFS::Get().ReadFile("shaders://pbr.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] PBR shaders not found");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.colorAttachmentFormat = m_OffscreenFormat;
        cfg.depthAttachmentFormat = m_DepthFormat;
        cfg.msaaSamples = m_Settings.msaaSamples;
        cfg.pushConstantSize = sizeof(PBRPushConstants);
        cfg.descriptorSetLayouts = {m_PbrSetLayout, m_Textures.GetBindlessLayout()};

        cfg.vertexInputBindings.resize(2);
        cfg.vertexInputBindings[0] = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        cfg.vertexInputBindings[1] = {1, sizeof(MeshInstance), VK_VERTEX_INPUT_RATE_INSTANCE};

        cfg.vertexInputAttributes.resize(12);
        cfg.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32) offsetof(Vertex, position)};
        cfg.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32) offsetof(Vertex, normal)};
        cfg.vertexInputAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, (u32) offsetof(Vertex, uv)};
        cfg.vertexInputAttributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (u32) offsetof(Vertex, tangent)};
        cfg.vertexInputAttributes[4] = {
            4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            (u32) offsetof(MeshInstance, modelMatrix)
        };
        cfg.vertexInputAttributes[5] = {
            5, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            (u32) offsetof(MeshInstance, modelMatrix) + 16
        };
        cfg.vertexInputAttributes[6] = {
            6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32) offsetof(MeshInstance, modelMatrix) + 32
        };
        cfg.vertexInputAttributes[7] = {
            7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32) offsetof(MeshInstance, modelMatrix) + 48
        };
        cfg.vertexInputAttributes[8] = {
            8, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            (u32) offsetof(MeshInstance, normalMatrix)
        };
        cfg.vertexInputAttributes[9] = {
            9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32) offsetof(MeshInstance, normalMatrix) + 16
        };
        cfg.vertexInputAttributes[10] = {
            10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32) offsetof(MeshInstance, normalMatrix) + 32
        };
        cfg.vertexInputAttributes[11] = {11, 1, VK_FORMAT_R32_UINT, (u32) offsetof(MeshInstance, materialIndex)};

        cfg.depthWriteEnable = VK_FALSE;
        cfg.depthCompareOp = VK_COMPARE_OP_EQUAL;

        m_PbrPipeline = CreateScope<Pipeline>(m_Context);
        m_PbrPipeline->BuildGraphics(vertSpv, fragSpv, cfg);

        PipelineConfigParams zCfg = cfg;
        zCfg.fragmentEntryPoint = "";
        zCfg.colorAttachmentFormat = VK_FORMAT_UNDEFINED;
        zCfg.depthWriteEnable = VK_TRUE;
        zCfg.depthCompareOp = VK_COMPARE_OP_LESS;

        m_ZPrepassPipeline = CreateScope<Pipeline>(m_Context);
        m_ZPrepassPipeline->BuildGraphics(vertSpv, {}, zCfg);

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
        cfg.colorAttachmentFormat = m_SwapchainFormat;
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

    void RendererImpl::BuildShadowPipeline() {
        auto vertSpv = VirtualFS::Get().ReadFile("shaders://shadow_depth.vert.spv");
        if (vertSpv.empty()) {
            LOG_ERROR("[Renderer] Shadow depth shader not found");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        cfg.msaaSamples = m_Settings.msaaSamples;
        cfg.pushConstantSize = sizeof(ShadowPushConstants);
        cfg.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
        cfg.descriptorSetLayouts = {m_PbrSetLayout};

        cfg.vertexInputBindings.resize(2);
        cfg.vertexInputBindings[0] = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        cfg.vertexInputBindings[1] = {1, sizeof(MeshInstance), VK_VERTEX_INPUT_RATE_INSTANCE};

        cfg.vertexInputAttributes.resize(9);
        cfg.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32) offsetof(Vertex, position)};
        cfg.vertexInputAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32) offsetof(Vertex, normal)};
        cfg.vertexInputAttributes[2] = {
            4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            (u32) offsetof(MeshInstance, modelMatrix)
        };
        cfg.vertexInputAttributes[3] = {
            5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32) offsetof(MeshInstance, modelMatrix) + 16
        };
        cfg.vertexInputAttributes[4] = {
            6, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            (u32) offsetof(MeshInstance, modelMatrix) + 32
        };
        cfg.vertexInputAttributes[5] = {
            7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32) offsetof(MeshInstance, modelMatrix) + 48
        };
        cfg.vertexInputAttributes[6] = {
            8, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            (u32) offsetof(MeshInstance, normalMatrix)
        };
        cfg.vertexInputAttributes[7] = {
            9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32) offsetof(MeshInstance, normalMatrix) + 16
        };
        cfg.vertexInputAttributes[8] = {
            10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32) offsetof(MeshInstance, normalMatrix) + 32
        };

        m_ShadowPipeline = CreateScope<Pipeline>(m_Context);
        m_ShadowPipeline->BuildShadowDepth(vertSpv, cfg);
    }

    void RendererImpl::BuildSkyboxPipeline() {
        auto vertSpv = VirtualFS::Get().ReadFile("shaders://skybox.vert.spv");
        auto fragSpv = VirtualFS::Get().ReadFile("shaders://skybox.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] Skybox shaders not found");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.colorAttachmentFormat = m_OffscreenFormat;
        cfg.depthAttachmentFormat = m_DepthFormat;
        cfg.msaaSamples = m_Settings.msaaSamples;
        cfg.descriptorSetLayouts = {m_SkyboxSetLayout};
        cfg.depthWriteEnable = VK_FALSE;
        cfg.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        cfg.vertexInputBindings.resize(1);
        cfg.vertexInputBindings[0] = {0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX};
        cfg.vertexInputAttributes.resize(1);
        cfg.vertexInputAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};

        m_SkyboxPipeline = CreateScope<Pipeline>(m_Context);
        m_SkyboxPipeline->BuildGraphics(vertSpv, fragSpv, cfg);
    }

    void RendererImpl::BuildDebugPipeline() {
        auto vertSpv = VirtualFS::Get().ReadFile("shaders://gizmo.vert.spv");
        auto fragSpv = VirtualFS::Get().ReadFile("shaders://gizmo.frag.spv");
        if (vertSpv.empty() || fragSpv.empty()) {
            LOG_ERROR("[Renderer] Debug draw shaders not found");
            return;
        }

        PipelineConfigParams cfg{};
        cfg.vertexEntryPoint = "main";
        cfg.fragmentEntryPoint = "main";
        cfg.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        cfg.colorAttachmentFormat = m_OffscreenFormat;
        cfg.depthAttachmentFormat = m_DepthFormat;
        cfg.msaaSamples = m_Settings.msaaSamples;
        cfg.vertexInputBindings = {
            {0, sizeof(DebugVertex), VK_VERTEX_INPUT_RATE_VERTEX}
        };
        cfg.vertexInputAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DebugVertex, position)},
            {1, 0, VK_FORMAT_R32_UINT, offsetof(DebugVertex, color)},
        };
        cfg.pushConstantSize = sizeof(Mat4);
        cfg.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
        cfg.depthWriteEnable = VK_FALSE;
        cfg.depthCompareOp = VK_COMPARE_OP_LESS;
        cfg.depthTestEnable = VK_TRUE;

        m_DebugPipeline = CreateScope<Pipeline>(m_Context);
        m_DebugPipeline->BuildGraphics(vertSpv, fragSpv, cfg);

        cfg.depthTestEnable = VK_FALSE;
        m_DebugPipelineNoDepth = CreateScope<Pipeline>(m_Context);
        m_DebugPipelineNoDepth->BuildGraphics(vertSpv, fragSpv, cfg);
    }

    void RendererImpl::FlushDebugDraw(VkCommandBuffer cb) {
        if (m_DebugVertices.empty() && m_DebugVerticesNoDepth.empty()) return;
        if (!m_DebugPipeline) return;

        FrameData &frame = m_Frames[m_CurrentFrame];

        auto recordLines = [&](std::vector<DebugVertex> &verts, Buffer *buf, Pipeline *pipeline) {
            if (verts.empty()) return;

            u32 count = static_cast<u32>(std::min(verts.size(), static_cast<size_t>(kMaxDebugVertices)));
            buf->LoadData(verts.data(), sizeof(DebugVertex) * count);

            VkRenderingAttachmentInfo color{};
            color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (m_Settings.msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
                color.imageView = m_MsaaColorImage.view;
                color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                color.resolveImageView = m_OffscreenColor.view;
                color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            } else {
                color.imageView = m_OffscreenColor.view;
                color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }

            VkRenderingAttachmentInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth.imageView = m_DepthImage.view;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo ri{};
            ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            ri.renderArea.extent = m_RenderExtent;
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &color;
            ri.pDepthAttachment = &depth;

            vkCmdBeginRendering(cb, &ri);

            VkViewport vp{
                0, 0,
                static_cast<float>(m_RenderExtent.width),
                static_cast<float>(m_RenderExtent.height), 0, 1
            };
            vkCmdSetViewport(cb, 0, 1, &vp);
            VkRect2D sc{{0, 0}, m_RenderExtent};
            vkCmdSetScissor(cb, 0, 1, &sc);

            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->GetHandle());

            Mat4 proj = m_ProjectionMatrix;
            proj[1][1] *= -1;
            Mat4 viewProj = proj * m_ViewMatrix;
            vkCmdPushConstants(cb, pipeline->GetLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &viewProj);

            VkBuffer vb = buf->GetHandle();
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
            vkCmdDraw(cb, count, 1, 0, 0);

            vkCmdEndRendering(cb);
        };

        recordLines(m_DebugVertices, frame.debugVertexBuffer.get(), m_DebugPipeline.get());
        recordLines(m_DebugVerticesNoDepth, frame.debugVertexBufferNoDepth.get(), m_DebugPipelineNoDepth.get());

        m_DebugVertices.clear();
        m_DebugVerticesNoDepth.clear();
    }

    void RendererImpl::DebugLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) {
        auto &target = depthTest ? m_DebugVertices : m_DebugVerticesNoDepth;
        DebugDraw::Line(target, a, b, color, depthTest);
    }

    void RendererImpl::DebugAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) {
        auto &target = depthTest ? m_DebugVertices : m_DebugVerticesNoDepth;
        DebugDraw::AABB(target, min, max, color, depthTest);
    }

    void RendererImpl::DebugBox(const Vec3 &center, const Vec3 &half,
                                const Mat4 &transform, u32 color, bool depthTest) {
        auto &target = depthTest ? m_DebugVertices : m_DebugVerticesNoDepth;
        DebugDraw::Box(target, center, half, transform, color, depthTest);
    }

    void RendererImpl::DebugSphere(const Vec3 &center, float radius,
                                   u32 color, int segments, bool depthTest) {
        auto &target = depthTest ? m_DebugVertices : m_DebugVerticesNoDepth;
        DebugDraw::Sphere(target, center, radius, color, segments, depthTest);
    }

    void RendererImpl::DebugFrustum(const Mat4 &invViewProj, u32 color, bool depthTest) {
        auto &target = depthTest ? m_DebugVertices : m_DebugVerticesNoDepth;
        DebugDraw::Frustum(target, invViewProj, color, depthTest);
    }

    void RendererImpl::DebugCross(const Vec3 &center, float size, u32 color, bool depthTest) {
        auto &target = depthTest ? m_DebugVertices : m_DebugVerticesNoDepth;
        DebugDraw::Cross(target, center, size, color, depthTest);
    }

    void RendererImpl::DebugAxes(const Mat4 &transform, float size) {
        DebugDraw::Axes(m_DebugVertices, transform, size);
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

    Scope<MaterialInstance> RendererImplCreateMaterialInstance(RendererImpl &impl, Ref<Material> mat) {
        return impl.CreateMaterialInstance(mat);
    }

    void RendererImplOnResize(RendererImpl &impl, u32 width, u32 height) { impl.OnResize(width, height); }
    float RendererImplGetAspectRatio(const RendererImpl &impl) { return impl.GetAspectRatio(); }
    TextureManager &RendererImplGetTextures(RendererImpl &impl) { return impl.GetTextures(); }
    MeshManager &RendererImplGetMeshes(RendererImpl &impl) { return impl.GetMeshes(); }
    void RendererImplSetSettings(RendererImpl &impl, const RenderSettings &settings) { impl.SetSettings(settings); }
    const RenderSettings &RendererImplGetSettingsConst(const RendererImpl &impl) { return impl.GetSettings(); }
    RenderSettings &RendererImplGetSettings(RendererImpl &impl) { return impl.GetSettings(); }
    const RendererConfig &RendererImplGetConfig(const RendererImpl &impl) { return impl.GetConfig(); }

    const FrameStats &RendererImplGetLastFrameStats(const RendererImpl &impl) { return impl.GetLastFrameStats(); }
    void RendererImplSetDebugUIEnabled(RendererImpl &impl, bool enabled) { impl.SetDebugUIEnabled(enabled); }
    bool RendererImplIsDebugUIEnabled(const RendererImpl &impl) { return impl.IsDebugUIEnabled(); }

    void RendererImplGetVramStats(const RendererImpl &impl, u64 &usage, u64 &budget) {
        impl.GetVramStats(usage, budget);
    }

    std::string RendererImplGetAdapterName(const RendererImpl &impl) { return impl.GetAdapterName(); }

    void RendererImplDebugLine(RendererImpl &impl, const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) {
        impl.DebugLine(a, b, color, depthTest);
    }

    void RendererImplDebugAABB(RendererImpl &impl, const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) {
        impl.DebugAABB(min, max, color, depthTest);
    }

    void RendererImplDebugBox(RendererImpl &impl, const Vec3 &center, const Vec3 &half, const Mat4 &transform,
                              u32 color, bool depthTest) {
        impl.DebugBox(center, half, transform, color, depthTest);
    }

    void RendererImplDebugSphere(RendererImpl &impl, const Vec3 &center, float radius, u32 color, int segments,
                                 bool depthTest) {
        impl.DebugSphere(center, radius, color, segments, depthTest);
    }

    void RendererImplDebugFrustum(RendererImpl &impl, const Mat4 &invViewProj, u32 color, bool depthTest) {
        impl.DebugFrustum(invViewProj, color, depthTest);
    }

    void RendererImplDebugCross(RendererImpl &impl, const Vec3 &center, float size, u32 color, bool depthTest) {
        impl.DebugCross(center, size, color, depthTest);
    }

    void RendererImplDebugAxes(RendererImpl &impl, const Mat4 &transform, float size) {
        impl.DebugAxes(transform, size);
    }
} // namespace Manro
