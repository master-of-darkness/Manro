#include <Manro/Render/Gui/ImGuiLayer.h>
#include <Manro/Platform/Window/IWindow.h>
#include <Manro/Core/Logger.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Render/Vulkan/VulkanContext.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <SDL3/SDL.h>

#include <imgui_vert_spv.h>
#include <imgui_frag_spv.h>

namespace Manro {
    ImGuiLayer::ImGuiLayer(const ImGuiLayerInfo& info) : m_Context(info.context) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        CreateDescriptorPool();
        SetupImGui(info);
    }

    ImGuiLayer::~ImGuiLayer() {
        vkDeviceWaitIdle(m_Context->GetDevice());
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        if (m_Pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Context->GetDevice(), m_Pool, nullptr);
        }
    }

    void ImGuiLayer::CreateDescriptorPool() {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (u32)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(m_Context->GetDevice(), &pool_info, nullptr, &m_Pool) != VK_SUCCESS) {
            LOG_ERROR("[ImGuiLayer] Failed to create descriptor pool");
        }
    }

    void ImGuiLayer::SetupImGui(const ImGuiLayerInfo& info) {
        ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, [](const char* function_name, void* user_data) {
            return vkGetInstanceProcAddr(static_cast<VkInstance>(user_data), function_name);
        }, m_Context->GetInstance());

        ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(info.window->GetNativeHandle()));

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_Context->GetInstance();
        init_info.PhysicalDevice = m_Context->GetPhysicalDevice();
        init_info.Device = m_Context->GetDevice();
        init_info.QueueFamily = m_Context->GetGraphicsQueueFamilyIndex();
        init_info.Queue = m_Context->GetGraphicsQueue();
        init_info.DescriptorPool = m_Pool;
        init_info.MinImageCount = info.imageCount;
        init_info.ImageCount = info.imageCount;
        init_info.UseDynamicRendering = true;

        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &info.colorFormat;

        init_info.CustomShaderVertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        init_info.CustomShaderVertCreateInfo.pCode = (const uint32_t*)imgui_vert_spv;
        init_info.CustomShaderVertCreateInfo.codeSize = imgui_vert_spv_len;

        init_info.CustomShaderFragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        init_info.CustomShaderFragCreateInfo.pCode = (const uint32_t*)imgui_frag_spv;
        init_info.CustomShaderFragCreateInfo.codeSize = imgui_frag_spv_len;

        if (!ImGui_ImplVulkan_Init(&init_info)) {
            LOG_ERROR("[ImGuiLayer] Failed to initialize ImGui Vulkan backend");
        }
    }

    void ImGuiLayer::NewFrame(const FrameStats& stats) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        float dt = 1000.0f / ImGui::GetIO().Framerate;
        m_FrameTimeHistory.push_back(dt);
        if (m_FrameTimeHistory.size() > MAX_HISTORY) {
            m_FrameTimeHistory.erase(m_FrameTimeHistory.begin());
        }

        if (m_Enabled) {
            DrawProfilerWindow(stats);
        }
    }

    void ImGuiLayer::DrawProfilerWindow(const FrameStats& stats) {
        if (ImGui::Begin("Manro Profiler", &m_Enabled)) {
            if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
                
                if (!m_FrameTimeHistory.empty()) {
                    ImGui::PlotLines("##FrameTime", m_FrameTimeHistory.data(), (int)m_FrameTimeHistory.size(), 0, nullptr, 0.0f, 33.3f, ImVec2(0, 80));
                }
            }

            if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Draw Calls: %u", stats.drawCalls);
                ImGui::Text("Triangles: %u", stats.triangleCount);
                ImGui::Text("Instances: %u", stats.instanceCount);
            }

            if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen)) {
                u64 usage, budget;
                m_Context->GetVramStats(usage, budget);
                float usageMB = static_cast<float>(usage) / (1024.f * 1024.f);
                float budgetMB = static_cast<float>(budget) / (1024.f * 1024.f);
                ImGui::Text("VRAM Usage: %.1f / %.1f MB", usageMB, budgetMB);
                ImGui::ProgressBar(usageMB / budgetMB, ImVec2(-FLT_MIN, 0));
                
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(m_Context->GetPhysicalDevice(), &props);
                ImGui::Text("GPU: %s", props.deviceName);
            }

            if (ImGui::CollapsingHeader("Scene")) {
                ImGui::Text("Active Lights: %u", stats.lightCount);
            }
            
            ImGui::Separator();
            ImGui::Text("Press Left Ctrl to toggle cursor");
        }
        ImGui::End();
    }

    void ImGuiLayer::Render(VkCommandBuffer cb) {
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        if (draw_data) {
            ImGui_ImplVulkan_RenderDrawData(draw_data, cb);
        }
    }
}
