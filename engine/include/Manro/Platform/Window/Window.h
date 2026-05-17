#pragma once

#include <Manro/Core/Types.h>
#include <functional>
#include <string>

namespace Manro {
    enum class WindowEvent {
        Close,
        Resized,
        FocusGained,
        FocusLost,
        Minimized,
        Restored,
    };

    struct WindowDesc_t {
        std::string Title = "Manro";
        u32 Width = 1280;
        u32 Height = 720;
        bool Resizable = true;
        bool Fullscreen = false;
    };

    class CWindow final {
    public:
        using EventCallback = std::function<void(WindowEvent, u32 w, u32 h)>;

        CWindow() = default;

        ~CWindow() { Shutdown(); }

        [[nodiscard]] bool Initialize(const WindowDesc_t &desc);

        void Shutdown();

        void SetTitle(const std::string &title);

        void Resize(u32 width, u32 height);

        void SetFullscreen(bool fullscreen);

        [[nodiscard]] bool IsFullscreen() const { return m_bFullscreen; }

        void ToggleFullscreen() { SetFullscreen(!IsFullscreen()); }

        [[nodiscard]] bool IsOpen() const { return m_bOpen; }

        u32 GetWidth() const { return m_unWidth; }

        u32 GetHeight() const { return m_unHeight; }

        std::string GetTitle() const { return m_Title; }

        void *GetNativeHandle() const;

        void SetEventCallback(EventCallback cb) { m_Callback = std::move(cb); }

        void ShowCursor(bool show);

        void CaptureMouse(bool capture);

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
