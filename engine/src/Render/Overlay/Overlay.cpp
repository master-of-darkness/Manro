#include "Overlay.h"
#include "Vulkan/VulkanContext.h"

#include <Manro/Interfaces/IWindow.h>
#include <Manro/Core/Logger.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <SDL3/SDL.h>


namespace Manro {
    COverlay::COverlay(const OverlayInfo_t &info) : m_Context(info.context) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        CreateDescriptorPool();
        SetupBackend(info);
    }

    COverlay::~COverlay() {
        vkDeviceWaitIdle(m_Context->GetDevice());
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        if (m_Pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_Context->GetDevice(), m_Pool, nullptr);
    }

    void COverlay::CreateDescriptorPool() {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
        };
        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = 1000 * IM_ARRAYSIZE(sizes);
        ci.poolSizeCount = (u32) IM_ARRAYSIZE(sizes);
        ci.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(m_Context->GetDevice(), &ci, nullptr, &m_Pool) != VK_SUCCESS)
            LOG_ERROR("[COverlay] Failed to create descriptor pool");
    }

    void COverlay::SetupBackend(const OverlayInfo_t &info) {
        ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3,
                                       [](const char *fn, void *ud) {
                                           return vkGetInstanceProcAddr(static_cast<VkInstance>(ud), fn);
                                       }, m_Context->GetInstance());

        ImGui_ImplSDL3_InitForVulkan(
            static_cast<SDL_Window *>(info.window->GetNativeHandle()));

        ImGui_ImplVulkan_InitInfo ii{};
        ii.Instance = m_Context->GetInstance();
        ii.PhysicalDevice = m_Context->GetPhysicalDevice();
        ii.Device = m_Context->GetDevice();
        ii.QueueFamily = m_Context->GetGraphicsQueueFamilyIndex();
        ii.Queue = m_Context->GetGraphicsQueue();
        ii.DescriptorPool = m_Pool;
        ii.MinImageCount = info.imageCount;
        ii.ImageCount = info.imageCount;
        ii.UseDynamicRendering = true;

        ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ii.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
                VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        ii.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        ii.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats =
                &info.colorFormat;

        if (!ImGui_ImplVulkan_Init(&ii))
            LOG_ERROR("[COverlay] Failed to initialize ImGui Vulkan backend");

        // thanks to https://github.com/ocornut/imgui/issues/707#issuecomment-4107169777
        ImGuiStyle &style = ImGui::GetStyle();
        ImVec4 *colors = style.Colors;

        // --- 1. Sizing & Spacing (Clean & Rigid) ---
        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 12.0f;

        // --- 2. Borders & Rounding (Technical/Drafting feel) ---
        style.WindowRounding = 2.0f;
        style.ChildRounding = 2.0f;
        style.FrameRounding = 2.0f;
        style.PopupRounding = 2.0f;
        style.ScrollbarRounding = 12.0f;
        style.GrabRounding = 2.0f;
        style.TabRounding = 2.0f;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 1.0f;

        // --- 3. Full Color Palette ---

        // Main Text & Background
        colors[ImGuiCol_Text] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f); // Deep Carbon Ink
        colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.94f, 1.00f); // Warm Paper
        colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);
        colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // Clean White Popups

        // Borders & Separators
        colors[ImGuiCol_Border] = ImVec4(0.75f, 0.75f, 0.72f, 1.00f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.80f, 0.80f, 0.78f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.17f, 0.34f, 0.59f, 0.78f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);

        // Frames (Inputs, Checkboxes, etc)
        colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.85f, 0.88f, 0.92f, 1.00f);

        // Titles & Menus
        colors[ImGuiCol_TitleBg] = ImVec4(0.92f, 0.92f, 0.90f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.88f, 0.88f, 0.86f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.92f, 0.92f, 0.90f, 0.75f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.92f, 0.92f, 0.90f, 1.00f);

        // Scrollbars
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.96f, 0.96f, 0.94f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.78f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.70f, 0.70f, 0.68f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.60f, 0.60f, 0.58f, 1.00f);

        // Interactables (Blueprint Blue)
        colors[ImGuiCol_CheckMark] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.17f, 0.34f, 0.59f, 0.70f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.17f, 0.34f, 0.59f, 0.08f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.17f, 0.34f, 0.59f, 0.20f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.17f, 0.34f, 0.59f, 0.35f);

        // Header (Selection in lists/trees)
        colors[ImGuiCol_Header] = ImVec4(0.17f, 0.34f, 0.59f, 0.12f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.17f, 0.34f, 0.59f, 0.25f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.17f, 0.34f, 0.59f, 0.40f);

        // Tables (Crucial for Light Mode)
        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.90f, 0.90f, 0.88f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.75f, 0.75f, 0.72f, 1.00f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.85f, 0.85f, 0.82f, 1.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4(0.92f, 0.92f, 0.90f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.92f, 0.92f, 0.90f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.96f, 0.96f, 0.94f, 1.00f);

        // Misc
        colors[ImGuiCol_PlotLines] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.17f, 0.34f, 0.59f, 0.25f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(0.17f, 0.34f, 0.59f, 0.90f);
        colors[ImGuiCol_NavHighlight] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);

#ifdef IMGUI_HAS_DOCK
        colors[ImGuiCol_DockingPreview] = ImVec4(0.17f, 0.34f, 0.59f, 0.40f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.96f, 0.96f, 0.94f, 1.00f);
#endif
    }

    void COverlay::NewFrame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame(); // TODO: move this exaclty to editor as in release nobody needs gizmos control
    }

    void COverlay::DrawDebugger(u32 drawCalls, u32 triangles, u32 instances,
                                const std::string &gpuName, RenderSettings_t &settings, bool &settingsChanged) {
        if (!m_bShowDebugUI) return;

        float frameTime = ImGui::GetIO().DeltaTime * 1000.0f;
        ImGui::SetNextWindowPos({10.f, 10.f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug", &m_bShowDebugUI, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("GPU: %s", gpuName.c_str());
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::Text("Frame Time: %.3f ms", frameTime);
            }
            if (ImGui::CollapsingHeader("Rendering Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Draw Calls: %u", drawCalls);
                ImGui::Text("Triangles: %u", triangles);
                ImGui::Text("Instances: %u", instances);
            }
            if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("VSync", &settings.enableVSync)) settingsChanged = true;
                if (ImGui::SliderFloat("Resolution Scale", &settings.resolutionScale, 0.1f, 2.0f))
                    settingsChanged = true;
                if (ImGui::SliderFloat("Max Draw Distance", &settings.maxDrawDistance, 1.0f, 50000.0f))
                    settingsChanged = true;

                ImGui::SeparatorText("Lighting");
                if (ImGui::SliderFloat("Gamma", &settings.lighting.gamma, 1.0f, 3.0f)) settingsChanged = true;
                if (ImGui::SliderFloat("Intensity", &settings.lighting.iblIntensity, 0.0f, 5.0f))
                    settingsChanged = true;

                ImGui::SeparatorText("Shadows");
                if (ImGui::Checkbox("Enable Shadows", &settings.shadows.enabled)) settingsChanged = true;
                if (ImGui::SliderFloat("Shadow Bias", &settings.shadows.bias, 0.0f, 0.1f, "%.4f"))
                    settingsChanged = true;

                ImGui::SeparatorText("Textures");
                {
                    static const char *kAnisoLabels[] = {"Off", "2x", "4x", "8x", "16x"};
                    static const float kAnisoValues[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
                    constexpr int kAnisoCount = IM_ARRAYSIZE(kAnisoValues);

                    int current = 0;
                    for (int i = 0; i < kAnisoCount; ++i) {
                        if (kAnisoValues[i] <= settings.textures.anisotropy + 0.001f) current = i;
                    }
                    if (ImGui::Combo("Anisotropic Filtering", &current, kAnisoLabels, kAnisoCount)) {
                        settings.textures.anisotropy = kAnisoValues[current];
                        settingsChanged = true;
                    }
                }

                ImGui::SeparatorText("Post Processing");
                if (ImGui::SliderFloat("Exposure", &settings.postProcess.tonemapping.exposure, 0.1f,
                                       10.0f))
                    settingsChanged = true;
            }
        }
        ImGui::End();
    }

    void COverlay::Render(VkCommandBuffer cb) {
        ImGui::Render();
        if (auto *dd = ImGui::GetDrawData())
            ImGui_ImplVulkan_RenderDrawData(dd, cb);
    }
} // namespace Manro
