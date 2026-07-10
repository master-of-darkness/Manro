#include "Internal/RendererBridge.h"
#include "Internal/FrameData.h"
#include "Internal/ShaderTypes.h"
#include "Internal/PassStates.h"
#include "Math/RenderMathUtils.h"
#include "Pipelines/MaterialSystem.h"
#include "Pipelines/PipelineManager.h"
#include "Scene/InstanceBatcher.h"
#include "Scene/GpuCullDispatcher.h"
#include "Scene/SceneRenderer.h"
#include "Resources/MeshManagerInternal.h"
#include "Resources/Material/Material.h"
#include "Resources/Texture/TextureManager.h"
#include "Targets/SwapchainManager.h"
#include "Targets/RenderTargetManager.h"
#include "Passes/ShadowSystem.h"
#include "Passes/SkyboxRenderer.h"
#include "Passes/DebugDrawSystem.h"
#include "Overlay/Overlay.h"
#include "Vulkan/VulkanContext.h"
#include <imgui_impl_vulkan.h>
#include "Vulkan/Buffer.h"
#include "Vulkan/Pipeline.h"
#include "Vulkan/DescriptorAllocator.h"
#include "Vulkan/PipelineCache.h"
#include "Core/Profiling.h"

#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/Model.h>
#include <Manro/Render/RendererConfig.h>
#include <VkBootstrap.h>
#include <stdexcept>
#include <algorithm>
#include <array>

namespace Manro {
    class CRendererImpl final {
    public:
        CRendererImpl(CWindow &window, CVirtualFS &vfs, u32 width, u32 height, const RenderSettings_t &settings,
                      const RendererConfig_t &config = RendererConfig_t::Default());

        ~CRendererImpl();

        bool BeginFramePace();

        void BeginFrameRecord();

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

        void SetViewProjection(const Mat4 &view, const Mat4 &proj) {
            m_ViewMatrix = view;
            m_ProjectionMatrix = proj;
        }

        void SetCameraPosition(const Vec3 &pos) { m_CameraPosition = pos; }

        void SetSkybox(TextureHandle cubemap) {
            if (cubemap == kInvalidTexture) {
                m_Skybox.ClearTexture();
                return;
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

        void WaitIdle() const {
            vkDeviceWaitIdle(m_Context.GetDevice());
        }

        MeshHandle UploadMesh(const ModelData_t &data) const { return m_Meshes.Upload(data); }

        TextureHandle UploadTexture(const TextureData_t &data) const { return m_Textures.Upload(data); }

        TextureHandle UploadCubemap(const std::vector<TextureData_t> &faces) const {
            return m_Textures.UploadCubemap(faces);
        }

        Ref<CMaterial> GetDefaultMaterial() const { return m_PipelineMgr.GetDefaultMaterial(); }


        static Scope<CMaterialInstance> CreateMaterialInstance(const Ref<CMaterial> &mat);

        void OnResize(u32 width, u32 height);

        float GetAspectRatio() const {
            if (m_unPendingHeight == 0) return 1.f;
            return static_cast<float>(m_unPendingWidth) / static_cast<float>(m_unPendingHeight);
        }

        void SetSettings(const RenderSettings_t &settings);

        const RenderSettings_t &GetSettings() const { return m_Settings; }

        RenderSettings_t &GetSettings() { return m_Settings; }

        const FrameStats_t &GetLastFrameStats() const { return m_LastFrameStats; }

        void SetDebugUIEnabled(bool enabled) const {
            if (m_Overlay) m_Overlay->SetDebugUIEnabled(enabled);
        }

        bool IsDebugUIEnabled() const {
            return m_Overlay && m_Overlay->IsDebugUIEnabled();
        }

        void DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) const;

        void DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) const;

        void DrawBox(const Vec3 &center, const Vec3 &half, const Mat4 &transform, u32 color, bool depthTest) const;

        void DrawSphere(const Vec3 &center, float radius, u32 color, int segments, bool depthTest) const;

        void *GetSceneTextureId();

    private:
        void CreateCommandBuffers();

        std::string GetAdapterName() const {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(m_Context.GetPhysicalDevice(), &props);
            return props.deviceName;
        }

        void InitAutoExposure();

        void ShutdownAutoExposure();

        void UpdateAutoExposureDescriptorSet(const FrameData_t &frame) const;

        void DispatchAutoExposure(VkCommandBuffer cb, FrameData_t &frame) const;

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

        CVulkanContext m_Context;
        Scope<CSceneRenderer> m_SceneRenderer;

        CTextureManager m_Textures;
        CMeshManager m_Meshes;

        std::vector<CPerFrameAllocator> m_PerFrameAlloc;
        CPersistentAllocator m_PersistentAlloc;
        CBindlessAllocator m_BindlessAlloc;
        CPipelineCache m_PipelineCache;

        Scope<COverlay> m_Overlay;

        CInstanceBatcher m_InstanceBatcher;

        Scope<CDrawSystem> m_DrawSystem;

        CSwapchainManager m_Swapchain;
        CRenderTargetManager m_RenderTargets;
        CShadowSystem m_Shadow;
        CSkyboxRenderer m_Skybox;

        CGpuCullDispatcher m_CullDispatcher;
        CPipelineManager m_PipelineMgr;
        CVirtualFS &m_Vfs;

        VkDescriptorSetLayout m_AutoExposureSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_AutoExposureDescriptorPool{VK_NULL_HANDLE};
        Scope<CPipeline> m_HistogramPipeline;
        Scope<CPipeline> m_AutoExposurePipeline;
        Scope<CBuffer> m_AutoExposureHistogramBuffer;
        Scope<CBuffer> m_AutoExposureLuminanceBuffer;

        std::vector<FrameData_t> m_Frames;
        u32 m_unCurrentFrame = 0;
        u32 m_unCurrentImageIndex = 0;

        CMaterialSystem m_MaterialSystem;

        std::vector<LightData> m_PendingLights;

        Mat4 m_ViewMatrix = Mat4(1.f);
        Mat4 m_ProjectionMatrix = Mat4(1.f);
        Vec3 m_CameraPosition = Vec3(0.f);

        u32 m_unPendingWidth = 0;
        u32 m_unPendingHeight = 0;
        bool m_bPendingResize = false;
        bool m_bPendingShadowRecreate = false;

        FrameStats_t m_CurrentFrameStats{};
        FrameStats_t m_LastFrameStats{};

        RenderSettings_t m_Settings{};
        RendererConfig_t m_Config{};
        VkExtent2D m_RenderExtent{};

        [[maybe_unused]] MnrGpuProfileCtx m_TracyGpuCtx{};

        VkDescriptorSet m_SceneImGuiTex{VK_NULL_HANDLE};
        bool m_bSceneTexDirty{true};
        u32 m_unLightGeneration{0};
    };

    CRendererImpl::CRendererImpl(CWindow &window, CVirtualFS &vfs, u32 width, u32 height,
                                 const RenderSettings_t &settings,
                                 const RendererConfig_t &config)
        : m_Context("ManroEngine", window),
          m_Textures(m_Context, m_BindlessAlloc), m_Meshes(new MeshManagerImpl_t(m_Context)),
          m_Swapchain(m_Context), m_RenderTargets(m_Context), m_Shadow(m_Context, vfs), m_Skybox(m_Context, vfs),
          m_CullDispatcher(m_Context, vfs),
          m_PipelineMgr(m_Context, vfs),
          m_Vfs(vfs),
          m_MaterialSystem(m_Context),
          m_Settings(settings), m_Config(config) {
        NormalizeRenderSettings(m_Settings, m_Context.GetMaxUsableSampleCount());
        m_Swapchain.Init(width, height, m_Settings.enableVSync);

        m_unPendingWidth = width;
        m_unPendingHeight = height;
        m_RenderExtent.width = std::max(1u, static_cast<u32>(width * m_Settings.resolutionScale));
        m_RenderExtent.height = std::max(1u, static_cast<u32>(height * m_Settings.resolutionScale));

        VkDevice device = m_Context.GetDevice();

        m_PerFrameAlloc.resize(GetFrameCount());
        for (auto &alloc: m_PerFrameAlloc)
            alloc.Init(device, 256);

        m_PersistentAlloc.Init(device, 64);
        m_BindlessAlloc.Init(device);
        m_Textures.InitDefaults();
        m_PipelineCache.Init(device);
        m_SceneRenderer = CreateScope<CSceneRenderer>();

        m_RenderTargets.Create(m_RenderExtent.width, m_RenderExtent.height,
                               ToVulkanSampleCount(m_Settings.msaaSamples));
        m_PipelineMgr.CreateDescriptorLayouts();
        m_CullDispatcher.Init();
        m_PipelineMgr.CreateDescriptorPool(GetFrameCount());

        m_Shadow.Init(m_PipelineMgr.GetDescriptorPool(), m_Settings.shadows, m_PipelineMgr.GetPbrSetLayout());

        m_Skybox.Init(m_PipelineMgr.GetDescriptorPool(), GetFrameCount(),
                      m_RenderTargets.GetOffscreenFormat(),
                      m_RenderTargets.GetDepthFormat(),
                      ToVulkanSampleCount(m_Settings.msaaSamples));

        m_MaterialSystem.Init();
        m_PipelineMgr.BuildPbrPipeline(m_RenderTargets, m_Textures, m_Settings);
        m_PipelineMgr.BuildCompositePipeline(m_Swapchain.GetFormat());
        m_CullDispatcher.BuildPipelines(m_PipelineCache);
        InitAutoExposure();

        m_DrawSystem = CreateScope<CDrawSystem>(m_Context, m_Vfs);
        m_DrawSystem->Init(m_RenderTargets.GetOffscreenFormat(),
                           m_RenderTargets.GetDepthFormat(),
                           ToVulkanSampleCount(m_Settings.msaaSamples));

        CreateCommandBuffers();
        m_Swapchain.CreateFrameSyncObjects(GetFrameCount());
        m_Swapchain.CreateRenderFinishedSemaphores();

#ifdef MANRO_PROFILING
        {
            VkCommandBuffer cb = m_Context.GetOneShotCommandBuffer();
            m_TracyGpuCtx = MNR_GPU_CONTEXT(
                m_Context.GetInstance(),
                m_Context.GetPhysicalDevice(),
                m_Context.GetDevice(),
                m_Context.GetGraphicsQueue(),
                cb);
            m_CullDispatcher.SetGpuProfileCtx(m_TracyGpuCtx);
            m_SceneRenderer->SetGpuProfileCtx(m_TracyGpuCtx);
            m_Shadow.SetGpuProfileCtx(m_TracyGpuCtx);
        }
#endif

        m_InstanceBatcher.Init(GetMaxInstances());
        m_PendingLights.reserve(GetMaxLights());

        OverlayInfo_t guiInfo{};
        guiInfo.context = &m_Context;
        guiInfo.window = &window;
        guiInfo.colorFormat = m_Swapchain.GetFormat();
        guiInfo.imageCount = m_Swapchain.GetImageCount();
        m_Overlay = CreateScope<COverlay>(guiInfo);
    }

    CRendererImpl::~CRendererImpl() {
        vkDeviceWaitIdle(m_Context.GetDevice());

#ifdef MANRO_PROFILING
        if (m_TracyGpuCtx)
            MNR_GPU_DESTROY(m_TracyGpuCtx);
#endif

        m_PipelineCache.Shutdown();
        m_BindlessAlloc.Shutdown();
        m_PersistentAlloc.Shutdown();
        for (auto &alloc: m_PerFrameAlloc)
            alloc.Shutdown();

        m_Overlay.reset();

        m_CullDispatcher.Shutdown();
        ShutdownAutoExposure();

        m_Shadow.Shutdown();
        m_Skybox.Shutdown();
        m_RenderTargets.Destroy();

        m_Swapchain.DestroyFrameSyncObjects();

        for (auto &f: m_Frames)
            if (f.commandPool)
                vkDestroyCommandPool(m_Context.GetDevice(), f.commandPool, nullptr);

        m_Swapchain.Shutdown();

        m_PipelineMgr.Shutdown();
    }

    void CRendererImpl::AddLight(const LightData &light) {
        if (m_PendingLights.size() < GetMaxLights()) {
            m_PendingLights.push_back(light);
            ++m_unLightGeneration;
        }
    }

    void CRendererImpl::ClearLights() {
        if (!m_PendingLights.empty()) {
            m_PendingLights.clear();
            ++m_unLightGeneration;
        }
    }

    void CRendererImpl::OnResize(u32 width, u32 height) {
        if (m_unPendingWidth == width && m_unPendingHeight == height &&
            m_Swapchain.GetExtent().width == width && m_Swapchain.GetExtent().height == height) {
            return;
        }
        m_unPendingWidth = width;
        m_unPendingHeight = height;
        m_bPendingResize = true;
    }

    void CRendererImpl::SetSettings(const RenderSettings_t &settings) {
        RenderSettings_t normalized = settings;
        NormalizeRenderSettings(normalized, m_Context.GetMaxUsableSampleCount());

        bool needsResize = (m_Settings.resolutionScale != normalized.resolutionScale) ||
                           (m_Settings.msaaSamples != normalized.msaaSamples) ||
                           (m_Settings.enableVSync != normalized.enableVSync);
        bool needsShadowRecreate = (m_Settings.shadows.resolution != normalized.shadows.resolution);

        m_Settings = normalized;

        if (needsShadowRecreate) {
            m_bPendingShadowRecreate = true;
        }

        if (needsResize) {
            m_bPendingResize = true;
        }
    }

    Scope<CMaterialInstance> CRendererImpl::CreateMaterialInstance(const Ref<CMaterial> &material) {
        return CreateScope<CMaterialInstance>(material);
    }

    void CRendererImpl::InitAutoExposure() {
        VkDescriptorSetLayoutBinding bindings[4]{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 4;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &m_AutoExposureSetLayout) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to create auto-exposure descriptor set layout");
        }

        VkDescriptorPoolSize sizes[3]{};
        sizes[0] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, GetFrameCount()};
        sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, GetFrameCount()};
        sizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GetFrameCount() * 2};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = sizes;
        poolInfo.maxSets = GetFrameCount();
        if (vkCreateDescriptorPool(m_Context.GetDevice(), &poolInfo, nullptr, &m_AutoExposureDescriptorPool) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to create auto-exposure descriptor pool");
        }

        m_AutoExposureHistogramBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 256,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_AutoExposureLuminanceBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(float),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        std::array<u32, 256> zeroHistogram{};
        m_AutoExposureHistogramBuffer->LoadData(zeroHistogram.data(), sizeof(zeroHistogram));
        constexpr float initialLuminance = 1.0f;
        m_AutoExposureLuminanceBuffer->LoadData(&initialLuminance, sizeof(initialLuminance));

        auto histogramSpv = m_Vfs.ReadFile("shaders://tonemapper_histogram.comp.spv");
        auto autoExposureSpv = m_Vfs.ReadFile("shaders://tonemapper_autoexposure.comp.spv");
        if (histogramSpv.empty() || autoExposureSpv.empty()) {
            LOG_WARN("[CRenderer] Auto-exposure shaders not found. Feature disabled.");
            return;
        }

        PipelineConfigParams_t histogramCfg{};
        histogramCfg.computeEntryPoint = "main";
        histogramCfg.pushConstantSize = sizeof(TonemapperData_t);
        histogramCfg.pushConstantStages = VK_SHADER_STAGE_COMPUTE_BIT;
        histogramCfg.descriptorSetLayouts = {m_AutoExposureSetLayout};

        m_HistogramPipeline = CreateScope<CPipeline>(m_Context);
        m_HistogramPipeline->BuildCompute(histogramSpv, histogramCfg);

        PipelineConfigParams_t autoExposureCfg = histogramCfg;

        m_AutoExposurePipeline = CreateScope<CPipeline>(m_Context);
        m_AutoExposurePipeline->BuildCompute(autoExposureSpv, autoExposureCfg);
    }

    void CRendererImpl::ShutdownAutoExposure() {
        m_AutoExposurePipeline.reset();
        m_HistogramPipeline.reset();
        m_AutoExposureLuminanceBuffer.reset();
        m_AutoExposureHistogramBuffer.reset();

        if (m_AutoExposureDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Context.GetDevice(), m_AutoExposureDescriptorPool, nullptr);
            m_AutoExposureDescriptorPool = VK_NULL_HANDLE;
        }
        if (m_AutoExposureSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_Context.GetDevice(), m_AutoExposureSetLayout, nullptr);
            m_AutoExposureSetLayout = VK_NULL_HANDLE;
        }
    }

    void CRendererImpl::UpdateAutoExposureDescriptorSet(const FrameData_t &frame) const {
        if (frame.autoExposureSet == VK_NULL_HANDLE ||
            !m_AutoExposureHistogramBuffer || !m_AutoExposureLuminanceBuffer) {
            return;
        }

        VkDescriptorImageInfo inColorI{};
        inColorI.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        inColorI.imageView = m_RenderTargets.GetOffscreenView();

        VkDescriptorImageInfo outImageI{};
        outImageI.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        outImageI.imageView = m_RenderTargets.GetOffscreenView();

        VkDescriptorBufferInfo histogramI{};
        histogramI.buffer = m_AutoExposureHistogramBuffer->GetHandle();
        histogramI.offset = 0;
        histogramI.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo luminanceI{};
        luminanceI.buffer = m_AutoExposureLuminanceBuffer->GetHandle();
        luminanceI.offset = 0;
        luminanceI.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[4]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frame.autoExposureSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &inColorI;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = frame.autoExposureSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &outImageI;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = frame.autoExposureSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &histogramI;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = frame.autoExposureSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &luminanceI;

        vkUpdateDescriptorSets(m_Context.GetDevice(), 4, writes, 0, nullptr);
    }

    void CRendererImpl::DispatchAutoExposure(VkCommandBuffer cb, FrameData_t &frame) const {
        if (m_Settings.postProcess.tonemapping.autoExposure != 1 ||
            !m_HistogramPipeline || !m_AutoExposurePipeline ||
            !m_AutoExposureHistogramBuffer || !m_AutoExposureLuminanceBuffer ||
            frame.autoExposureSet == VK_NULL_HANDLE) {
            return;
        }

        TonemapperData_t tm = m_Settings.postProcess.tonemapping;
        tm.inputMatrix = SlangFloat3x3_t(glm::mat3(tm.exposure));

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_HistogramPipeline->GetLayout(), 0, 1, &frame.autoExposureSet, 0, nullptr);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_HistogramPipeline->GetHandle());
        vkCmdPushConstants(cb, m_HistogramPipeline->GetLayout(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TonemapperData_t), &tm);
        const u32 groupsX = (m_RenderExtent.width + 15) / 16;
        const u32 groupsY = (m_RenderExtent.height + 15) / 16;
        vkCmdDispatch(cb, groupsX, groupsY, 1);

        VkBufferMemoryBarrier2 histogramBarrier{};
        histogramBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        histogramBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        histogramBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        histogramBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        histogramBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        histogramBarrier.buffer = m_AutoExposureHistogramBuffer->GetHandle();
        histogramBarrier.offset = 0;
        histogramBarrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo histogramDep{};
        histogramDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        histogramDep.bufferMemoryBarrierCount = 1;
        histogramDep.pBufferMemoryBarriers = &histogramBarrier;
        vkCmdPipelineBarrier2(cb, &histogramDep);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_AutoExposurePipeline->GetLayout(), 0, 1, &frame.autoExposureSet, 0, nullptr);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_AutoExposurePipeline->GetHandle());
        vkCmdPushConstants(cb, m_AutoExposurePipeline->GetLayout(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TonemapperData_t), &tm);
        vkCmdDispatch(cb, 1, 1, 1);

        VkBufferMemoryBarrier2 luminanceBarrier{};
        luminanceBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        luminanceBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        luminanceBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        luminanceBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        luminanceBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        luminanceBarrier.buffer = m_AutoExposureLuminanceBuffer->GetHandle();
        luminanceBarrier.offset = 0;
        luminanceBarrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo luminanceDep{};
        luminanceDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        luminanceDep.bufferMemoryBarrierCount = 1;
        luminanceDep.pBufferMemoryBarriers = &luminanceBarrier;
        vkCmdPipelineBarrier2(cb, &luminanceDep);
    }

    void CRendererImpl::RecreateSwapchain() {
        const u32 w = m_unPendingWidth;
        const u32 h = m_unPendingHeight;
        m_bPendingResize = false;
        if (w == 0 || h == 0) return;

        m_Swapchain.Recreate(w, h, m_Settings.enableVSync);

        m_RenderTargets.Destroy();
        m_RenderExtent.width = std::max(1u, static_cast<u32>(w * m_Settings.resolutionScale));
        m_RenderExtent.height = std::max(1u, static_cast<u32>(h * m_Settings.resolutionScale));
        m_RenderTargets.Create(m_RenderExtent.width, m_RenderExtent.height,
                               ToVulkanSampleCount(m_Settings.msaaSamples));

        m_PipelineMgr.BuildPbrPipeline(m_RenderTargets, m_Textures, m_Settings);
        m_PipelineMgr.BuildCompositePipeline(m_Swapchain.GetFormat());
        m_Skybox.RebuildPipeline(m_RenderTargets.GetOffscreenFormat(),
                                 m_RenderTargets.GetDepthFormat(),
                                 ToVulkanSampleCount(m_Settings.msaaSamples));
        m_DrawSystem->Shutdown();
        m_DrawSystem->Init(m_RenderTargets.GetOffscreenFormat(),
                           m_RenderTargets.GetDepthFormat(),
                           ToVulkanSampleCount(m_Settings.msaaSamples));

        for (u32 i = 0; i < GetFrameCount(); ++i) {
            m_PipelineMgr.UpdateCompositeDescriptorSet(
                i, m_Frames[i], m_RenderTargets,
                m_AutoExposureLuminanceBuffer->GetHandle());
            UpdateAutoExposureDescriptorSet(m_Frames[i]);
            m_Shadow.UpdatePbrDescriptorSetShadow(m_Frames[i].pbrSet);
        }
        m_bSceneTexDirty = true;
        m_Swapchain.SetNeedsRecreate(false);
    }

    void CRendererImpl::FinalizeFrameAndPresent(VkCommandBuffer cb) {
        MNR_PROFILE_FUNCTION();
        {
            MNR_GPU_ZONE(m_TracyGpuCtx, cb, "Present Transition");
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            b.dstAccessMask = 0;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            b.image = m_Swapchain.GetImage(m_unCurrentImageIndex);
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
        m_Swapchain.SetImageLayout(m_unCurrentImageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        MNR_GPU_COLLECT(m_TracyGpuCtx, cb);

        if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
            throw std::runtime_error("Failed to end frame command buffer");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSemaphores[] = {m_Swapchain.GetImageAvailableSemaphore(m_unCurrentFrame)};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cb;
        VkSemaphore signalSemaphores[] = {m_Swapchain.GetRenderFinishedSemaphore(m_unCurrentImageIndex)};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        VkFence submitFence = m_Swapchain.GetInFlightFence(m_unCurrentFrame);
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
        presentInfo.pImageIndices = &m_unCurrentImageIndex;

        VkResult presentResult = vkQueuePresentKHR(m_Context.GetGraphicsQueue(), &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            m_Swapchain.SetNeedsRecreate(true);
        } else if (presentResult != VK_SUCCESS) {
            throw std::runtime_error("Failed to present swapchain image");
        }

        m_unCurrentFrame = (m_unCurrentFrame + 1) % GetFrameCount();
    }

    bool CRendererImpl::BeginFramePace() {
        MNR_PROFILE_FUNCTION();

        // Handle recreate before acquiring a new swapchain image.
        // Acquiring first and then bailing out can leave frame fences unsignaled.
        if (m_unPendingWidth == 0 || m_unPendingHeight == 0) return false;
        {
            MNR_PROFILE_SCOPE("SwapchainRecreate");
            if (m_bPendingResize || m_Swapchain.NeedsRecreate()) {
                RecreateSwapchain();
                return false;
            }
            if (m_bPendingShadowRecreate) {
                std::vector<VkDescriptorSet> pbrSets;
                pbrSets.reserve(m_Frames.size());
                for (auto &f: m_Frames) pbrSets.push_back(f.pbrSet);
                m_Shadow.Recreate(m_PipelineMgr.GetDescriptorPool(), m_Settings.shadows,
                                  m_PipelineMgr.GetPbrSetLayout(),
                                  pbrSets);
                m_bPendingShadowRecreate = false;
                return false;
            }
        }

        VkDevice device = m_Context.GetDevice();

        {
            MNR_PROFILE_SCOPE("WaitForFence");
            VkFence inFlightFence = m_Swapchain.GetInFlightFence(m_unCurrentFrame);
            vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
        }

        {
            MNR_PROFILE_SCOPE("AcquireSwapchainImage");
            VkResult acquireResult = vkAcquireNextImageKHR(device, m_Swapchain.GetHandle(), UINT64_MAX,
                                                           m_Swapchain.GetImageAvailableSemaphore(m_unCurrentFrame),
                                                           VK_NULL_HANDLE, &m_unCurrentImageIndex);
            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
                m_Swapchain.SetNeedsRecreate(true);
                return false;
            }
            if (acquireResult == VK_SUBOPTIMAL_KHR) {
                m_Swapchain.SetNeedsRecreate(true);
            } else if (acquireResult != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    void CRendererImpl::BeginFrameRecord() {
        MNR_PROFILE_FUNCTION();

        {
            MNR_PROFILE_SCOPE("TextureUploads");
            m_Textures.FlushPendingUploads();
        }

        VkDevice device = m_Context.GetDevice();
        FrameData_t &frame = m_Frames[m_unCurrentFrame];

        {
            MNR_PROFILE_SCOPE("ResetCommandPool");
            VkFence inFlightFence = m_Swapchain.GetInFlightFence(m_unCurrentFrame);
            vkResetFences(device, 1, &inFlightFence);
            vkResetCommandPool(device, frame.commandPool, 0);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS) {
                throw std::runtime_error("Failed to begin frame command buffer");
            }
        }

        {
            MNR_PROFILE_SCOPE("FrameStateReset");
            m_PerFrameAlloc[m_unCurrentFrame].Reset();

            m_LastFrameStats = m_CurrentFrameStats;
            m_InstanceBatcher.ClearFrameInstances();

            m_CurrentFrameStats.Reset();
            m_CurrentFrameStats.drawCalls = m_InstanceBatcher.GetStaticInstanceCount();
            m_CurrentFrameStats.instanceCount = m_CurrentFrameStats.drawCalls;
            m_CurrentFrameStats.triangleCount = m_InstanceBatcher.GetStaticTriangleCount();
        }

        {
            MNR_PROFILE_SCOPE("DrawSystemBeginFrame");
            m_DrawSystem->BeginFrame();
        }

        {
            MNR_PROFILE_SCOPE("OverlayUpdate");
            m_Overlay->NewFrame();

            if (m_Overlay->IsDebugUIEnabled()) {
                bool settingsChanged = false;
                RenderSettings_t editedSettings = m_Settings;
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
        }

        if (frame.commandBuffer == VK_NULL_HANDLE) {
            throw std::runtime_error("Command buffer is not available");
        }
    }

    void CRendererImpl::UploadLights(u32 frameIndex) {
        m_CurrentFrameStats.lightCount = static_cast<u32>(m_PendingLights.size());
        FrameData_t &frame = m_Frames[frameIndex];
        if (frame.lightUploadedGeneration != m_unLightGeneration && !m_PendingLights.empty()) {
            frame.lightBuffer->LoadData(
                m_PendingLights.data(),
                sizeof(LightData) * m_PendingLights.size());
            frame.lightUploadedGeneration = m_unLightGeneration;
        }
    }

    void CRendererImpl::BeginRendering() {
        MNR_PROFILE_FUNCTION();
        FrameData_t &frame = m_Frames[m_unCurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;
        VkExtent2D ext = m_RenderExtent;

        {
            MNR_PROFILE_SCOPE("UploadLights");
            UploadLights(m_unCurrentFrame);
        }

        u32 totalInstCount = m_InstanceBatcher.GetTotalInstanceCount();

        if (totalInstCount > 0) {
            MNR_GPU_ZONE(m_TracyGpuCtx, cb, "GPU Culling");
            m_InstanceBatcher.UploadToGpu(frame);

            m_CullDispatcher.Dispatch({
                .cb = cb,
                .frame = frame,
                .totalInstCount = totalInstCount,
                .viewMatrix = m_ViewMatrix,
                .projectionMatrix = m_ProjectionMatrix,
                .cameraPosition = m_CameraPosition,
                .settings = m_Settings,
                .shadow = m_Shadow,
                .lights = m_PendingLights,
                .meshes = m_Meshes,
                .renderExtent = ext,
                .maxLightsPerTile = GetMaxLightsPerTile(),
                .maxTilesX = GetMaxTilesX(),
                .maxTilesY = GetMaxTilesY(),
                .tileSize = GetTileSize(),
            });
        }


        {
            VkImageMemoryBarrier2 b[3]{};
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

            const VkImageLayout depthOldLayout = m_RenderTargets.GetDepthLayout();
            if (m_RenderTargets.GetDepthImage() != VK_NULL_HANDLE &&
                depthOldLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                VkImageMemoryBarrier2 depthBarrier{};
                depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                depthBarrier.srcStageMask = (depthOldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                                ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                                                : (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
                depthBarrier.srcAccessMask = (depthOldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                                 ? 0
                                                 : (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
                depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                depthBarrier.oldLayout = depthOldLayout;
                depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthBarrier.image = m_RenderTargets.GetDepthImage();
                depthBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                b[barrierCount++] = depthBarrier;
                m_RenderTargets.SetDepthLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            }

            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = barrierCount;
            dep.pImageMemoryBarriers = b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        UniformBufferObject_t ubo{};
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
        ubo.materialCount = static_cast<int>(m_MaterialSystem.GetMaterialCount());
        frame.uboBuffer->LoadData(&ubo, sizeof(ubo));

        m_MaterialSystem.FlushToGpu();

        {
            VkBufferMemoryBarrier2 b[5]{};
            b[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            b[0].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            b[0].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
            b[0].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b[0].buffer = m_MaterialSystem.GetMaterialBufferHandle();
            b[0].offset = 0;
            b[0].size = VK_WHOLE_SIZE;
            b[1] = b[0];
            b[1].buffer = m_MaterialSystem.GetTextureInfoBufferHandle();
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

    void CRendererImpl::RenderQueue() {
        MNR_PROFILE_FUNCTION();
        FrameData_t &frame = m_Frames[m_unCurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;

        u32 instanceCount = m_InstanceBatcher.GetTotalInstanceCount();
        auto *indexBuffer = m_Meshes.GetIndexBuffer();
        auto *vertexBuffer = m_Meshes.GetVertexBuffer();

        const bool hasMeshes = (instanceCount > 0 && m_PipelineMgr.GetZPrepassPipeline() && m_PipelineMgr.
                                GetPbrPipeline() && indexBuffer &&
                                vertexBuffer &&
                                m_RenderTargets.GetDepthView() != VK_NULL_HANDLE);
        const bool hasSkybox = m_Skybox.IsValid();

        Internal::ZPrepassPassState_t zState{};
        if (hasMeshes || m_RenderTargets.GetDepthView() != VK_NULL_HANDLE) {
            zState.extent = m_RenderExtent;
            zState.depthView = m_RenderTargets.GetDepthView();
            if (hasMeshes) {
                zState.pipeline = m_PipelineMgr.GetZPrepassPipeline()->GetHandle();
                zState.pipelineLayout = m_PipelineMgr.GetZPrepassPipeline()->GetLayout();
                zState.descriptorSets[0] = frame.pbrSet;
                zState.descriptorSets[1] = m_Textures.GetBindlessSet();
                zState.descriptorSetCount = 2;
                zState.indexBuffer = indexBuffer->GetHandle();
                zState.vertexBuffers[0] = vertexBuffer->GetHandle();
                zState.vertexBuffers[1] = frame.instanceBuffer->GetHandle();
                zState.indirectBuffer = frame.indirectBuffer->GetHandle();
                zState.countBuffer = frame.countBuffer->GetHandle();
                zState.instanceCount = instanceCount;
                zState.drawStride = sizeof(DrawCommand_t);
            }
            m_SceneRenderer->SetZPrepassState(&zState);
        }

        Internal::PbrPassState_t pbrState{};
        if (hasMeshes) {
            pbrState.extent = m_RenderExtent;
            pbrState.msaaSamples = ToVulkanSampleCount(m_Settings.msaaSamples);
            pbrState.msaaColorView = m_RenderTargets.GetMsaaView();
            pbrState.offscreenColorView = m_RenderTargets.GetOffscreenView();
            pbrState.depthView = m_RenderTargets.GetDepthView();
            pbrState.pipeline = m_PipelineMgr.GetPbrPipeline()->GetHandle();
            pbrState.pipelineLayout = m_PipelineMgr.GetPbrPipeline()->GetLayout();
            pbrState.descriptorSets[0] = frame.pbrSet;
            pbrState.descriptorSets[1] = m_Textures.GetBindlessSet();
            pbrState.descriptorSetCount = 2;
            pbrState.indexBuffer = indexBuffer->GetHandle();
            pbrState.vertexBuffers[0] = vertexBuffer->GetHandle();
            pbrState.vertexBuffers[1] = frame.instanceBuffer->GetHandle();
            pbrState.indirectBuffer = frame.indirectBuffer->GetHandle();
            pbrState.countBuffer = frame.countBuffer->GetHandle();
            pbrState.instanceCount = instanceCount;
            pbrState.drawStride = sizeof(DrawCommand_t);
            m_SceneRenderer->SetPbrPassState(&pbrState);
        }

        Internal::SkyboxPassState_t skyState{};
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

        {
            MNR_GPU_ZONE(m_TracyGpuCtx, cb, "Scene Passes");
            m_SceneRenderer->Flush(cb);
        }

        if (!hasMeshes && !hasSkybox && m_RenderTargets.GetOffscreenView() != VK_NULL_HANDLE) {
            MNR_GPU_ZONE(m_TracyGpuCtx, cb, "Clear Empty Scene");

            VkRenderingAttachmentInfo colorAtt{};
            colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAtt.imageView = m_RenderTargets.GetOffscreenView();
            colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAtt.clearValue.color = {{0.05f, 0.05f, 0.07f, 1.f}};

            VkRenderingInfo ri{};
            ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            ri.renderArea.extent = m_RenderExtent;
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &colorAtt;

            vkCmdBeginRendering(cb, &ri);
            vkCmdEndRendering(cb);
        }
    }


    void CRendererImpl::EndRendering() {
        MNR_PROFILE_FUNCTION();
        FrameData_t &frame = m_Frames[m_unCurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;

        {
            MNR_GPU_ZONE(m_TracyGpuCtx, cb, "Debug Draw");
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

    void CRendererImpl::EndFrameAndPresent() {
        MNR_PROFILE_FUNCTION();
        FrameData_t &frame = m_Frames[m_unCurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;
        VkExtent2D ext{m_Swapchain.GetExtent().width, m_Swapchain.GetExtent().height};

        const bool runAutoExposure =
                m_Settings.postProcess.tonemapping.autoExposure == 1 &&
                m_HistogramPipeline && m_AutoExposurePipeline &&
                m_AutoExposureHistogramBuffer && m_AutoExposureLuminanceBuffer &&
                frame.autoExposureSet != VK_NULL_HANDLE;
        {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstStageMask = runAutoExposure
                                 ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                                 : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = runAutoExposure
                                  ? (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT)
                                  : VK_ACCESS_2_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = runAutoExposure
                              ? VK_IMAGE_LAYOUT_GENERAL
                              : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = m_RenderTargets.GetOffscreenImage();
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        if (runAutoExposure) {
            DispatchAutoExposure(cb, frame);

            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
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
            b.oldLayout = m_Swapchain.GetImageLayout(m_unCurrentImageIndex);
            b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.image = m_Swapchain.GetImage(m_unCurrentImageIndex);
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
        m_Swapchain.SetImageLayout(m_unCurrentImageIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        {
            CompositePushConstants_t cpc{};
            cpc.tm = m_Settings.postProcess.tonemapping;
            cpc.tm.inputMatrix = SlangFloat3x3_t(glm::mat3(cpc.tm.exposure));
            cpc.imageSize = Vec2(static_cast<float>(m_RenderExtent.width), static_cast<float>(m_RenderExtent.height));
            cpc.bloomIntensity = m_Settings.postProcess.bloomIntensity;
            cpc.bloomThreshold = m_Settings.postProcess.bloomThreshold;
            cpc.bloomEnabled = m_Settings.postProcess.enableBloom ? 1 : 0;

            Internal::CompositePassState_t compositeState{};
            compositeState.extent = ext;
            compositeState.colorView = m_Swapchain.GetImageView(m_unCurrentImageIndex);
            compositeState.pipeline = m_PipelineMgr.GetCompositePipeline()->GetHandle();
            compositeState.pipelineLayout = m_PipelineMgr.GetCompositePipeline()->GetLayout();
            compositeState.descriptorSet = frame.compositeSet;
            compositeState.pushConstants = &cpc;
            compositeState.pushConstantSize = sizeof(CompositePushConstants_t);
            compositeState.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;

            m_SceneRenderer->SetCompositePassState(&compositeState);
            m_SceneRenderer->Flush(cb);
        }

        {
            MNR_GPU_ZONE(m_TracyGpuCtx, cb, "GUI COverlay");
            VkRenderingAttachmentInfo guiColorAtt{};
            guiColorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            guiColorAtt.imageView = m_Swapchain.GetImageView(m_unCurrentImageIndex);
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

    void CRendererImpl::DrawMesh(MeshHandle meshId, CMaterialInstance &material, const Mat4 &model) {
        m_InstanceBatcher.DrawMesh(meshId, material, model, m_Meshes, m_MaterialSystem, m_CurrentFrameStats);
    }

    void CRendererImpl::DrawModel(const CModel &model, const Mat4 &transform) {
        m_InstanceBatcher.DrawModel(model, transform, m_Meshes, m_MaterialSystem, m_CurrentFrameStats);
    }

    void CRendererImpl::DrawModelStatic(const CModel &model, const Mat4 &transform) {
        m_InstanceBatcher.DrawModelStatic(model, transform, m_Meshes, m_MaterialSystem);
    }

    void CRendererImpl::DrawMeshStatic(MeshHandle meshId, CMaterialInstance &material, const Mat4 &model) {
        m_InstanceBatcher.DrawMeshStatic(meshId, material, model, m_Meshes, m_MaterialSystem);
    }

    void CRendererImpl::ClearStaticDraws() {
        m_InstanceBatcher.ClearStaticDraws();
    }

    void *CRendererImpl::GetSceneTextureId() {
        if (m_bSceneTexDirty) {
            if (m_SceneImGuiTex != VK_NULL_HANDLE)
                ImGui_ImplVulkan_RemoveTexture(m_SceneImGuiTex);
            m_SceneImGuiTex = ImGui_ImplVulkan_AddTexture(
                m_RenderTargets.GetOffscreenView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_bSceneTexDirty = false;
        }
        return m_SceneImGuiTex;
    }

    void CRendererImpl::CreateCommandBuffers() {
        const u32 frameCount = GetFrameCount();
        const u32 maxLights = GetMaxLights();
        const u32 maxInstances = GetMaxInstances();
        const u32 maxTiles = GetMaxTiles();
        const u32 maxLightsPerTile = GetMaxLightsPerTile();

        m_Frames.resize(frameCount);

        for (u32 i = 0; i < frameCount; ++i) {
            FrameData_t &f = m_Frames[i];

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

            f.uboBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(UniformBufferObject_t),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.lightBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(LightData) * maxLights,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            struct TileHeader_t {
                u32 offset, count, pad0, pad1;
            };
            f.tileHeaderBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(TileHeader_t) * maxTiles,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

            f.tileLightIndexBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(u32) * maxTiles * maxLightsPerTile,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

            f.instanceBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(MeshInstance_t) * maxInstances,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.cullDataBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(CullData_t) * maxInstances,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

            f.indirectBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(DrawCommand_t) * maxInstances,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            f.countBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(u32),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            f.shadowIndirectBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(DrawCommand_t) * maxInstances,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            f.shadowCountBuffer = CreateScope<CBuffer>(
                m_Context, sizeof(u32),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            m_PipelineMgr.AllocateFrameDescriptorSets(f, m_CullDispatcher, m_Shadow, m_Skybox);

            if (m_AutoExposureSetLayout != VK_NULL_HANDLE && m_AutoExposureDescriptorPool != VK_NULL_HANDLE) {
                VkDescriptorSetAllocateInfo autoDsAI{};
                autoDsAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                autoDsAI.descriptorPool = m_AutoExposureDescriptorPool;
                autoDsAI.descriptorSetCount = 1;
                autoDsAI.pSetLayouts = &m_AutoExposureSetLayout;
                if (vkAllocateDescriptorSets(m_Context.GetDevice(), &autoDsAI, &f.autoExposureSet) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to allocate auto-exposure descriptor set");
                }
            }

            m_PipelineMgr.UpdateCompositeDescriptorSet(
                i, f, m_RenderTargets,
                m_AutoExposureLuminanceBuffer->GetHandle());
            m_PipelineMgr.UpdatePbrDescriptorSet(i, f, m_MaterialSystem, m_Textures, m_Shadow, m_Skybox);
            UpdateAutoExposureDescriptorSet(f);
        }
    }

    void CRendererImpl::DrawLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) const {
        m_DrawSystem->SubmitLine(a, b, color, depthTest);
    }

    void CRendererImpl::DrawAABB(const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) const {
        m_DrawSystem->SubmitBox((min + max) * 0.5f, (max - min) * 0.5f, Mat4(1.0f), color, depthTest);
    }

    void CRendererImpl::DrawBox(const Vec3 &center, const Vec3 &half,
                                const Mat4 &transform, u32 color, bool depthTest) const {
        m_DrawSystem->SubmitBox(center, half, transform, color, depthTest);
    }

    void CRendererImpl::DrawSphere(const Vec3 &center, float radius,
                                   u32 color, int segments, bool depthTest) const {
        m_DrawSystem->SubmitSphere(center, radius, color, segments, depthTest);
    }

    Scope<CRendererImpl> CreateRendererImpl(CWindow &window, CVirtualFS &vfs, u32 width, u32 height,
                                            const RenderSettings_t &settings, const RendererConfig_t &config) {
        return CreateScope<CRendererImpl>(window, vfs, width, height, settings, config);
    }

    CRenderer::CRenderer(CWindow &window, CVirtualFS &vfs, u32 width, u32 height, const RenderSettings_t &settings)
        : CRenderer(window, vfs, width, height, settings, RendererConfig_t::Default()) {
    }

    CRenderer::CRenderer(CWindow &window, CVirtualFS &vfs, u32 width, u32 height, const RenderSettings_t &settings,
                         const RendererConfig_t &config)
        : m_Impl(CreateRendererImpl(window, vfs, width, height, settings, config)) {
    }

    CRenderer::~CRenderer() = default;

    bool RendererImplBeginFramePace(CRendererImpl &impl) { return impl.BeginFramePace(); }
    void RendererImplBeginFrameRecord(CRendererImpl &impl) { impl.BeginFrameRecord(); }
    void RendererImplBeginRendering(CRendererImpl &impl) { impl.BeginRendering(); }
    void RendererImplRenderQueue(CRendererImpl &impl) { impl.RenderQueue(); }
    void RendererImplEndRendering(CRendererImpl &impl) { impl.EndRendering(); }
    void RendererImplEndFrameAndPresent(CRendererImpl &impl) { impl.EndFrameAndPresent(); }

    void RendererImplDrawMesh(CRendererImpl &impl, MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model) {
        impl.DrawMesh(mesh, mat, model);
    }

    void RendererImplDrawMeshStatic(CRendererImpl &impl, MeshHandle mesh, CMaterialInstance &mat, const Mat4 &model) {
        impl.DrawMeshStatic(mesh, mat, model);
    }

    void RendererImplClearStaticDraws(CRendererImpl &impl) { impl.ClearStaticDraws(); }

    void RendererImplDrawModel(CRendererImpl &impl, const CModel &model, const Mat4 &transform) {
        impl.DrawModel(model, transform);
    }

    void RendererImplDrawModelStatic(CRendererImpl &impl, const CModel &model, const Mat4 &transform) {
        impl.DrawModelStatic(model, transform);
    }

    void RendererImplAddLight(CRendererImpl &impl, const LightData &light) { impl.AddLight(light); }
    void RendererImplClearLights(CRendererImpl &impl) { impl.ClearLights(); }

    void RendererImplSetViewProjection(CRendererImpl &impl, const Mat4 &view, const Mat4 &proj) {
        impl.SetViewProjection(view, proj);
    }

    void RendererImplSetCameraPosition(CRendererImpl &impl, const Vec3 &pos) { impl.SetCameraPosition(pos); }
    void RendererImplSetSkybox(CRendererImpl &impl, TextureHandle cubemap) { impl.SetSkybox(cubemap); }
    MeshHandle RendererImplUploadMesh(const CRendererImpl &impl, const ModelData_t &data) { return impl.
            UploadMesh(data);
    }

    TextureHandle RendererImplUploadTexture(const CRendererImpl &impl, const TextureData_t &data) {
        return impl.UploadTexture(data);
    }

    TextureHandle RendererImplUploadCubemap(const CRendererImpl &impl, const std::vector<TextureData_t> &faces) {
        return impl.UploadCubemap(faces);
    }

    Ref<CMaterial> RendererImplGetDefaultMaterial(const CRendererImpl &impl) { return impl.GetDefaultMaterial(); }

    Scope<CMaterialInstance> RendererImplCreateMaterialInstance(CRendererImpl &impl, const Ref<CMaterial> &mat) {
        return CRendererImpl::CreateMaterialInstance(mat);
    }

    void RendererImplOnResize(CRendererImpl &impl, u32 width, u32 height) { impl.OnResize(width, height); }
    float RendererImplGetAspectRatio(const CRendererImpl &impl) { return impl.GetAspectRatio(); }
    void RendererImplSetSettings(CRendererImpl &impl, const RenderSettings_t &settings) { impl.SetSettings(settings); }
    const RenderSettings_t &RendererImplGetSettingsConst(const CRendererImpl &impl) { return impl.GetSettings(); }
    RenderSettings_t &RendererImplGetSettings(CRendererImpl &impl) { return impl.GetSettings(); }

    const FrameStats_t &RendererImplGetLastFrameStats(const CRendererImpl &impl) { return impl.GetLastFrameStats(); }
    void RendererImplSetDebugUIEnabled(const CRendererImpl &impl, bool enabled) { impl.SetDebugUIEnabled(enabled); }
    bool RendererImplIsDebugUIEnabled(const CRendererImpl &impl) { return impl.IsDebugUIEnabled(); }

    void *RendererImplGetSceneTextureId(CRendererImpl &impl) { return impl.GetSceneTextureId(); }

    void RendererImplDrawLine(const CRendererImpl &impl, const Vec3 &a, const Vec3 &b, u32 color, bool depthTest) {
        impl.DrawLine(a, b, color, depthTest);
    }

    void RendererImplDrawAABB(const CRendererImpl &impl, const Vec3 &min, const Vec3 &max, u32 color, bool depthTest) {
        impl.DrawAABB(min, max, color, depthTest);
    }

    void RendererImplDrawBox(const CRendererImpl &impl, const Vec3 &center, const Vec3 &half, const Mat4 &transform,
                             u32 color, bool depthTest) {
        impl.DrawBox(center, half, transform, color, depthTest);
    }

    void RendererImplDrawSphere(const CRendererImpl &impl, const Vec3 &center, float radius, u32 color, int segments,
                                bool depthTest) {
        impl.DrawSphere(center, radius, color, segments, depthTest);
    }

    void RendererImplWaitIdle(const CRendererImpl &impl) {
        impl.WaitIdle();
    }
} // namespace Manro
