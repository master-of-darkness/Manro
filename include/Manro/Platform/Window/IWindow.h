#pragma once

#include <Manro/Core/Types.h>
#include <string>
#include <functional>

namespace Engine {
    enum class WindowEvent {
        Close,
        Resized,
        FocusGained,
        FocusLost,
        Minimized,
        Restored,
    };

    struct WindowDesc {
        std::string Title = "Engine";
        u32 Width = 1280;
        u32 Height = 720;
        bool Resizable = true;
        bool Fullscreen = false;
    };

    class IWindow {
    public:
        virtual ~IWindow() = default;

        virtual bool Initialize(const WindowDesc &desc) = 0;

        virtual void Shutdown() = 0;

        virtual void SetTitle(const std::string &title) = 0;

        virtual void Resize(u32 width, u32 height) = 0;

        virtual void SetFullscreen(bool fullscreen) = 0;

        virtual bool IsFullscreen() const = 0;

        virtual void ToggleFullscreen() { SetFullscreen(!IsFullscreen()); }

        virtual bool IsOpen() const = 0;

        virtual u32 GetWidth() const = 0;

        virtual u32 GetHeight() const = 0;

        virtual std::string GetTitle() const = 0;

        virtual void *GetNativeHandle() const = 0;

        using EventCallback = std::function<void(WindowEvent, u32 w, u32 h)>; // TODO: use??

        virtual void SetEventCallback(EventCallback cb) = 0;

        virtual void ShowCursor(bool show) = 0;

        virtual void CaptureMouse(bool capture) = 0;
    };
} // namespace Engine
