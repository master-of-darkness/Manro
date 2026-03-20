#pragma once
#include <Manro/Core/Types.h>
#include <Manro/Render/Vulkan/VulkanContext.h>
#include <Manro/Platform/Window/IWindow.h>
#include <volk.h>
#include <vector>

namespace Manro {
    struct ImGuiLayerInfo {
        VulkanContext *context = nullptr;
        IWindow *window = nullptr;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        u32 imageCount = 2;
    };

    class ImGuiLayer {
    public:
        explicit ImGuiLayer(const ImGuiLayerInfo &info);

        ~ImGuiLayer();

        void NewFrame();

        void Render(VkCommandBuffer cb);

        bool IsEnabled() const { return m_Enabled; }
        void SetEnabled(bool e) { m_Enabled = e; }

    private:
        void CreateDescriptorPool();

        void SetupBackend(const ImGuiLayerInfo &info);

        VulkanContext *m_Context = nullptr;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        bool m_Enabled = true;
    };
} // namespace Manro
