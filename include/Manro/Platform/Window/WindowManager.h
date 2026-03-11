#pragma once

#include <Manro/Platform/Window/IWindow.h>
#include <Manro/Core/Types.h>
#include <unordered_map>
#include <string>

namespace Engine {
    using WindowHandle = u32;
    inline constexpr WindowHandle kInvalidWindow = 0;

    class WindowManager {
    public:
        WindowManager() = default;

        ~WindowManager() { ShutdownAll(); }

        WindowManager(const WindowManager &) = delete;

        WindowManager &operator=(const WindowManager &) = delete;

        WindowHandle AddWindow(const WindowDesc &desc);

        void DestroyWindow(WindowHandle handle);

        void ShutdownAll();

        IWindow *Get(WindowHandle handle);

        const IWindow *Get(WindowHandle handle) const;

        bool IsValid(WindowHandle handle) const;

        IWindow *GetPrimary();

        void DispatchWindowEvent(u32 sdlWindowId, u32 eventType, u32 data1, u32 data2);

    private:
        std::unordered_map<WindowHandle, Scope<IWindow> > m_Windows;

        std::unordered_map<u32, WindowHandle> m_SDLIdToHandle;

        WindowHandle m_NextHandle{1};
        WindowHandle m_PrimaryHandle{kInvalidWindow};
    };
} // namespace Engine
