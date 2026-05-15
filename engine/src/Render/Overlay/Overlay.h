#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>
#include <Manro/Interfaces/IWindow.h>
#include <Manro/Render/RenderSettings.h>
#include <string>

namespace Manro {
    class CVulkanContext;

    struct OverlayInfo_t {
        CVulkanContext *context = nullptr;
        IWindow *window = nullptr;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        u32 imageCount = 2;
    };

    class COverlay {
    public:
        explicit COverlay(const OverlayInfo_t &info);

        ~COverlay();

        void NewFrame();

        void DrawDebugger(u32 drawCalls, u32 triangles, u32 instances,
                          const std::string &gpuName, RenderSettings_t &settings, bool &settingsChanged);

        void Render(VkCommandBuffer cb);

        bool IsDebugUIEnabled() const { return m_bShowDebugUI; }

        void SetDebugUIEnabled(bool e) { m_bShowDebugUI = e; }

    private:
        void CreateDescriptorPool();

        void SetupBackend(const OverlayInfo_t &info);

        CVulkanContext *m_Context = nullptr;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        bool m_bShowDebugUI = true;
    };
} // namespace Manro