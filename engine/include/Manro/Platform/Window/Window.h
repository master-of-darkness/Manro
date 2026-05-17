#pragma once

#include <Manro/Interfaces/IWindow.h>
#include <string>

namespace Manro {
    class CWindow final : public IWindow {
    public:
        CWindow() = default;

        ~CWindow() override { Shutdown(); }

        // IWindow / IAppSystem
        [[nodiscard]] bool Initialize(const WindowDesc_t &desc) override;

        void Shutdown() override;

        void SetTitle(const std::string &title) override;

        void Resize(u32 width, u32 height) override;

        void SetFullscreen(bool fullscreen) override;

        [[nodiscard]] bool IsFullscreen() const override { return m_bFullscreen; }

        [[nodiscard]] bool IsOpen() const override { return m_bOpen; }

        u32 GetWidth() const override { return m_unWidth; }

        u32 GetHeight() const override { return m_unHeight; }

        std::string GetTitle() const override { return m_Title; }

        void *GetNativeHandle() const override;

        void SetEventCallback(EventCallback cb) override { m_Callback = std::move(cb); }

        void ShowCursor(bool show) override;

        void CaptureMouse(bool capture) override;

        void OnPlatformWindowEvent(u32 platformWindowEventId, u32 data1, u32 data2);

        u32 GetPlatformWindowID() const;

    private:
        void *m_Handle{nullptr};
        EventCallback m_Callback;
        std::string m_Title;
        u32 m_unWidth{0};
        u32 m_unHeight{0};
        bool m_bOpen{false};
        bool m_bFullscreen{false};
    };
} // namespace Manro
