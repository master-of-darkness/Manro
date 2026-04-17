#pragma once

#include <Manro/Interfaces/IWindow.h>
#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <unordered_map>
#include <string>

namespace Manro {
    class CWindowManager {
    public:
        CWindowManager() = default;

        ~CWindowManager() { ShutdownAll(); }

        CWindowManager(const CWindowManager &) = delete;

        CWindowManager &operator=(const CWindowManager &) = delete;

        WindowHandle AddWindow(const WindowDesc_t &desc);

        void DestroyWindow(WindowHandle handle);

        void ShutdownAll();

        IWindow *Get(WindowHandle handle);

        const IWindow *Get(WindowHandle handle) const;

        bool IsValid(WindowHandle handle) const;

        IWindow *GetPrimary();

        void DispatchWindowEvent(u32 platformWindowId, u32 eventType, u32 data1, u32 data2);

    private:
        std::unordered_map<WindowHandle, Scope<IWindow> > m_Windows;
        std::unordered_map<u32, WindowHandle> m_PlatformIdToHandle;

        WindowHandle m_nNextHandle{WindowHandle::Make(1, 0)};
        WindowHandle m_PrimaryHandle{kInvalidWindow};
    };
} // namespace Manro