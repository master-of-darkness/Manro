#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Interfaces/IAppSystem.h>
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

    class IWindow : public IAppSystem {
    public:
        ~IWindow() override = default;

        [[nodiscard]] virtual bool Initialize(const WindowDesc_t &desc) = 0;

        virtual void SetTitle(const std::string &title) = 0;

        virtual void Resize(u32 width, u32 height) = 0;

        virtual void SetFullscreen(bool fullscreen) = 0;

        [[nodiscard]] virtual bool IsFullscreen() const = 0;

        virtual void ToggleFullscreen() { SetFullscreen(!IsFullscreen()); }

        [[nodiscard]] virtual bool IsOpen() const = 0;

        virtual u32 GetWidth() const = 0;

        virtual u32 GetHeight() const = 0;

        virtual std::string GetTitle() const = 0;

        virtual void *GetNativeHandle() const = 0;

        using EventCallback = std::function<void(WindowEvent, u32 w, u32 h)>;

        virtual void SetEventCallback(EventCallback cb) = 0;

        virtual void ShowCursor(bool show) = 0;

        virtual void CaptureMouse(bool capture) = 0;
    };
} // namespace Manro