#pragma once

#include "IWindow.h"

struct SDL_Window;

namespace Engine {
    class SDL3Window final : public IWindow {
    public:
        SDL3Window() = default;

        ~SDL3Window() override { Shutdown(); }

        bool Initialize(const WindowDesc &desc) override;

        void Shutdown() override;

        void SetTitle(const std::string &title) override;

        void Resize(u32 width, u32 height) override;

        void SetFullscreen(bool fullscreen) override;

        bool IsOpen() const override { return m_Open; }
        u32 GetWidth() const override { return m_Width; }
        u32 GetHeight() const override { return m_Height; }
        std::string GetTitle() const override { return m_Title; }

        void *GetNativeHandle() const override;

        void SetEventCallback(EventCallback cb) override { m_Callback = std::move(cb); }

        void ShowCursor(bool show) override;

        void CaptureMouse(bool capture) override;

        void OnSDLEvent(u32 sdlWindowEventId, u32 data1, u32 data2);

        u32 GetSDLWindowID() const;

    private:
        SDL_Window *m_Handle{nullptr};
        EventCallback m_Callback;
        std::string m_Title;
        u32 m_Width{0};
        u32 m_Height{0};
        bool m_Open{false};
    };
} // namespace Engine