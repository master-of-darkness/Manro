#pragma once
#include <Manro/Core/Types.h>
#include <vulkan/vulkan.h>
#include <imgui.h>

namespace Manro {
    class VulkanContext;
    class IWindow;

    struct ImGuiLayerInfo {
        VulkanContext* context;
        IWindow* window;
        VkFormat colorFormat;
        u32 imageCount;
    };

    struct FrameStats;

    class ImGuiLayer {
    public:
        ImGuiLayer(const ImGuiLayerInfo& info);
        ~ImGuiLayer();

        void NewFrame(const FrameStats& stats);
        void Render(VkCommandBuffer cb);

        void SetEnabled(bool enabled) { m_Enabled = enabled; }
        bool IsEnabled() const { return m_Enabled; }

    private:
        void CreateDescriptorPool();
        void SetupImGui(const ImGuiLayerInfo& info);
        void DrawProfilerWindow(const FrameStats& stats);

        VulkanContext* m_Context;
        VkDescriptorPool m_Pool{VK_NULL_HANDLE};
        bool m_Enabled{true};

        std::vector<float> m_FrameTimeHistory;
        static constexpr size_t MAX_HISTORY = 120;
    };
}
