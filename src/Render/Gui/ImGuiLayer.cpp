#include <Manro/Render/Gui/ImGuiLayer.h>
#include <Manro/Platform/Window/IWindow.h>
#include <Manro/Core/Logger.h>
#include "../Backend/Vulkan/VulkanContext.h"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <SDL3/SDL.h>

#include <imgui_vert_spv.h>
#include <imgui_frag_spv.h>

namespace Manro {
    ImGuiLayer::ImGuiLayer(const ImGuiLayerInfo &info) : m_Context(info.context) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        CreateDescriptorPool();
        SetupBackend(info);
    }

    ImGuiLayer::~ImGuiLayer() {
        vkDeviceWaitIdle(m_Context->GetDevice());
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        if (m_Pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_Context->GetDevice(), m_Pool, nullptr);
    }

    void ImGuiLayer::CreateDescriptorPool() {
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
            LOG_ERROR("[ImGuiLayer] Failed to create descriptor pool");
    }

    void ImGuiLayer::SetupBackend(const ImGuiLayerInfo &info) {
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

        ii.CustomShaderVertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ii.CustomShaderVertCreateInfo.pCode = (const uint32_t *) imgui_vert_spv;
        ii.CustomShaderVertCreateInfo.codeSize = imgui_vert_spv_len;
        ii.CustomShaderFragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ii.CustomShaderFragCreateInfo.pCode = (const uint32_t *) imgui_frag_spv;
        ii.CustomShaderFragCreateInfo.codeSize = imgui_frag_spv_len;

        if (!ImGui_ImplVulkan_Init(&ii))
            LOG_ERROR("[ImGui] Failed to initialize ImGui Vulkan backend");
    }

    void ImGuiLayer::NewFrame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::DrawDebugUI(u32 drawCalls, u32 triangles, u32 instances,
                                 const std::string& gpuName, RenderSettings& settings, bool& settingsChanged) {
        if (!m_ShowDebugUI) return;

        float frameTime = ImGui::GetIO().DeltaTime * 1000.0f;
        ImGui::SetNextWindowPos({10.f, 10.f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug", &m_ShowDebugUI, ImGuiWindowFlags_AlwaysAutoResize)) {
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
                if (ImGui::SliderFloat("Resolution Scale", &settings.resolutionScale, 0.1f, 2.0f)) settingsChanged = true;

                ImGui::SeparatorText("Lighting");
                if (ImGui::SliderFloat("Gamma", &settings.lighting.gamma, 1.0f, 3.0f)) settingsChanged = true;
                if (ImGui::SliderFloat("Intensity", &settings.lighting.iblIntensity, 0.0f, 5.0f)) settingsChanged = true;

                ImGui::SeparatorText("Shadows");
                if (ImGui::Checkbox("Enable Shadows", &settings.shadows.enabled)) settingsChanged = true;
                if (ImGui::SliderFloat("Shadow Bias", &settings.shadows.bias, 0.0f, 0.1f, "%.4f")) settingsChanged = true; // TODO: fix me

                ImGui::SeparatorText("Post Processing");
                if (ImGui::SliderFloat("Exposure", &settings.postProcess.tonemapping.exposure, 0.1f, 10.0f)) settingsChanged = true;
            }
        }
        ImGui::End();
    }

    void ImGuiLayer::Render(VkCommandBuffer cb) {
        ImGui::Render();
        if (auto *dd = ImGui::GetDrawData())
            ImGui_ImplVulkan_RenderDrawData(dd, cb);
    }
} // namespace Manro
