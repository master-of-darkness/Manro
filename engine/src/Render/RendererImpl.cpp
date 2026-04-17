#include "Internal/RendererInternal.h"
#include "Internal/RendererTypes.h"
#include "Internal/RenderMathUtils.h"
#include "Internal/MaterialSystem.h"
#include "Internal/InstanceBatcher.h"
#include "Internal/GpuCullDispatcher.h"
#include "Internal/PipelineManager.h"
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
#include <../Core/Profiling.h>
#include <../Core/Profiling.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/Model.h>
#include <Manro/Render/RendererConfig.h>
#include <VkBootstrap.h>
#include <stdexcept>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>


namespace Manro {

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
            if (cubemap == kInvalidTexture)
            LOG_ERROR("[Renderer] SetSkybox called with invalid texture!");
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

        Ref<Material> GetDefaultMaterial() const { return m_PipelineMgr.GetDefaultMaterial(); }


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
        void CreateCommandBuffers();

        void CreateSyncObjects();

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

        InstanceBatcher m_InstanceBatcher;

        Scope<DrawSystem> m_DrawSystem;

        SwapchainManager m_Swapchain;
        RenderTargetManager m_RenderTargets;
        ShadowSystem m_Shadow;
        SkyboxRenderer m_Skybox;

        GpuCullDispatcher m_CullDispatcher;
        PipelineManager m_PipelineMgr;

        std::vector<FrameData> m_Frames;
        u32 m_CurrentFrame = 0;
        u32 m_CurrentImageIndex = 0;

        MaterialSystem m_MaterialSystem;

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

        MnrGpuProfileCtx m_TracyGpuCtx{};
    };

    RendererImpl::RendererImpl(IWindow &window, u32 width, u32 height,
                               const RenderSettings &settings,
                               const RendererConfig &config)
        : m_Context("ManroEngine", window),
          m_Textures(m_Context, m_BindlessAlloc), m_Meshes(new MeshManagerImpl(m_Context)),
          m_Swapchain(m_Context), m_RenderTargets(m_Context), m_Shadow(m_Context), m_Skybox(m_Context),
          m_CullDispatcher(m_Context),
          m_PipelineMgr(m_Context),
          m_MaterialSystem(m_Context),
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

        m_DrawSystem = CreateScope<DrawSystem>(m_Context);
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

#ifdef MANRO_PROFILING
        if (m_TracyGpuCtx) MNR_GPU_DESTROY(m_TracyGpuCtx);
#endif

        m_PipelineCache.Shutdown();
        m_BindlessAlloc.Shutdown();
        m_PersistentAlloc.Shutdown();
        for (auto &alloc: m_PerFrameAlloc)
            alloc.Shutdown();

        m_Overlay.reset();

        m_CullDispatcher.Shutdown();

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

        m_PipelineMgr.BuildPbrPipeline(m_RenderTargets, m_Textures, m_Settings);
        m_PipelineMgr.BuildCompositePipeline(m_Swapchain.GetFormat());
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
            m_PipelineMgr.UpdateCompositeDescriptorSet(i, m_Frames[i], m_RenderTargets);
            m_Shadow.UpdatePbrDescriptorSetShadow(m_Frames[i].pbrSet);
        }
        m_Swapchain.SetNeedsRecreate(false);
    }

    void RendererImpl::FinalizeFrameAndPresent(VkCommandBuffer cb) {
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
            b.image = m_Swapchain.GetImage(m_CurrentImageIndex);
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        }
        m_Swapchain.SetImageLayout(m_CurrentImageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        MNR_GPU_COLLECT(m_TracyGpuCtx, cb);

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
        MNR_PROFILE_FUNCTION();

        // Handle recreate before acquiring a new swapchain image.
        // Acquiring first and then bailing out can leave frame fences unsignaled.
        if (m_PendingWidth == 0 || m_PendingHeight == 0) return false;
        {
            MNR_PROFILE_SCOPE("SwapchainRecreate");
            if (m_PendingResize || m_Swapchain.NeedsRecreate()) {
                RecreateSwapchain();
                return false;
            }
            if (m_PendingShadowRecreate) {
                std::vector<VkDescriptorSet> pbrSets;
                pbrSets.reserve(m_Frames.size());
                for (auto &f: m_Frames) pbrSets.push_back(f.pbrSet);
                m_Shadow.Recreate(m_PipelineMgr.GetDescriptorPool(), m_Settings.shadows,
                                  m_PipelineMgr.GetPbrSetLayout(),
                                  pbrSets);
                m_PendingShadowRecreate = false;
                return false;
            }
        }

        {
            MNR_PROFILE_SCOPE("TextureUploads");
            m_Textures.FlushPendingUploads();
        }

        VkDevice device = m_Context.GetDevice();
        FrameData &frame = m_Frames[m_CurrentFrame];

        {
            MNR_PROFILE_SCOPE("WaitForFence");
            VkFence inFlightFence = m_Swapchain.GetInFlightFence(m_CurrentFrame);
            vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
        }

        {
            MNR_PROFILE_SCOPE("AcquireSwapchainImage");
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
        }

        {
            MNR_PROFILE_SCOPE("ResetCommandPool");
            VkFence inFlightFence = m_Swapchain.GetInFlightFence(m_CurrentFrame);
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
            m_PerFrameAlloc[m_CurrentFrame].Reset();

            m_LastFrameStats = m_CurrentFrameStats;
            m_InstanceBatcher.ClearFrameInstances();

            m_CurrentFrameStats.Reset();
            m_CurrentFrameStats.drawCalls = m_InstanceBatcher.GetStaticInstanceCount();
            m_CurrentFrameStats.instanceCount = m_InstanceBatcher.GetStaticInstanceCount();
            m_CurrentFrameStats.triangleCount = m_InstanceBatcher.GetStaticTriangleCount();
        }

        {
            MNR_PROFILE_SCOPE("DrawSystemBeginFrame");
            if (m_DrawSystem) {
                m_DrawSystem->BeginFrame();
            }
        }

        {
            MNR_PROFILE_SCOPE("OverlayUpdate");
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
        }

        if (frame.commandBuffer == VK_NULL_HANDLE) {
            throw std::runtime_error("Command buffer is not available");
        }

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
        MNR_PROFILE_FUNCTION();
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;
        VkExtent2D ext = m_RenderExtent;

        {
            MNR_PROFILE_SCOPE("UploadLights");
            UploadLights(m_CurrentFrame);
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

    void RendererImpl::RenderQueue() {
        MNR_PROFILE_FUNCTION();
        FrameData &frame = m_Frames[m_CurrentFrame];

        u32 instanceCount = m_InstanceBatcher.GetTotalInstanceCount();
        auto *indexBuffer = m_Meshes.GetIndexBuffer();
        auto *vertexBuffer = m_Meshes.GetVertexBuffer();

        const bool hasMeshes = (instanceCount > 0 && m_PipelineMgr.GetZPrepassPipeline() && m_PipelineMgr.
                                GetPbrPipeline() && indexBuffer &&
                                vertexBuffer &&
                                m_RenderTargets.GetDepthView() != VK_NULL_HANDLE);
        const bool hasSkybox = m_Skybox.IsValid();

        Internal::ZPrepassPassState zState{};
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
            MNR_GPU_ZONE(m_TracyGpuCtx, frame.commandBuffer, "Scene Passes");
            m_SceneRenderer->Flush(frame.commandBuffer);
        }
    }


    void RendererImpl::EndRendering() {
        MNR_PROFILE_FUNCTION();
        FrameData &frame = m_Frames[m_CurrentFrame];
        VkCommandBuffer cb = frame.commandBuffer;

        if (m_DrawSystem) {
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

    void RendererImpl::EndFrameAndPresent() {
        MNR_PROFILE_FUNCTION();
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
            compositeState.pipeline = m_PipelineMgr.GetCompositePipeline()->GetHandle();
            compositeState.pipelineLayout = m_PipelineMgr.GetCompositePipeline()->GetLayout();
            compositeState.descriptorSet = frame.compositeSet;
            compositeState.pushConstants = &cpc;
            compositeState.pushConstantSize = sizeof(CompositePushConstants);
            compositeState.pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT;

            m_SceneRenderer->SetCompositePassState(&compositeState);
            m_SceneRenderer->Flush(cb);
        }

        if (m_Overlay) {
            MNR_GPU_ZONE(m_TracyGpuCtx, cb, "GUI Overlay");
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

    void RendererImpl::DrawMesh(MeshHandle meshId, MaterialInstance &material, const Mat4 &model) {
        m_InstanceBatcher.DrawMesh(meshId, material, model, m_Meshes, m_MaterialSystem, m_CurrentFrameStats);
    }

    void RendererImpl::DrawModel(const Model &model, const Mat4 &transform) {
        m_InstanceBatcher.DrawModel(model, transform, m_Meshes, m_MaterialSystem, m_CurrentFrameStats);
    }

    void RendererImpl::DrawModelStatic(const Model &model, const Mat4 &transform) {
        m_InstanceBatcher.DrawModelStatic(model, transform, m_Meshes, m_MaterialSystem);
        m_InstanceBatcher.InvalidateStaticUpload(m_Frames);
    }

    void RendererImpl::DrawMeshStatic(MeshHandle meshId, MaterialInstance &material, const Mat4 &model) {
        m_InstanceBatcher.DrawMeshStatic(meshId, material, model, m_Meshes, m_MaterialSystem);
        m_InstanceBatcher.InvalidateStaticUpload(m_Frames);
    }

    void RendererImpl::ClearStaticDraws() {
        m_InstanceBatcher.ClearStaticDraws();
        m_InstanceBatcher.InvalidateStaticUpload(m_Frames);
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

            m_PipelineMgr.AllocateFrameDescriptorSets(f, m_CullDispatcher, m_Shadow, m_Skybox);


            m_PipelineMgr.UpdateCompositeDescriptorSet(i, f, m_RenderTargets);
            m_PipelineMgr.UpdatePbrDescriptorSet(i, f, m_MaterialSystem, m_Textures, m_Shadow, m_Skybox);
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
