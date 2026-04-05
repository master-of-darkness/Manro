#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>
#include <Manro/Interfaces/IWindow.h>
#include <Manro/Render/RenderSettings.h>
#include <vector>
#include <string>

namespace Manro {
    class VulkanContext;

    struct ImGuiLayerInfo {
        VulkanContext *context = nullptr;
        IWindow *window = nullptr;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        u32 imageCount = 2;
    };

    class Overlay {
    public:
        explicit Overlay(const ImGuiLayerInfo &info);

        ~Overlay();

        void NewFrame();

        void DrawDebugUI(u32 drawCalls, u32 triangles, u32 instances,
                         const std::string &gpuName, RenderSettings &settings, bool &settingsChanged);

        void Render(VkCommandBuffer cb);

        bool IsDebugUIEnabled() const { return m_ShowDebugUI; }

        void SetDebugUIEnabled(bool e) { m_ShowDebugUI = e; }

    private:
        void CreateDescriptorPool();

        void SetupBackend(const ImGuiLayerInfo &info);

        VulkanContext *m_Context = nullptr;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        bool m_ShowDebugUI = true;
    };
} // namespace Manro
