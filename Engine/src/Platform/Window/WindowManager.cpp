#include "WindowManager.h"
#include "SDL3Window.h"
#include <Core/Logger.h>

namespace Engine {
    WindowHandle WindowManager::AddWindow(const WindowDesc &desc) {
        auto window = CreateScope<SDL3Window>();
        if (!window->Initialize(desc)) {
            LOG_ERROR("[WindowManager] Failed to create window '{}'", desc.Title);
            return kInvalidWindow;
        }

        WindowHandle handle = m_NextHandle++;

        u32 sdlId = static_cast<SDL3Window *>(window.get())->GetSDLWindowID();
        m_SDLIdToHandle[sdlId] = handle;

        m_Windows.emplace(handle, std::move(window));

        if (m_PrimaryHandle == kInvalidWindow)
            m_PrimaryHandle = handle;

        LOG_INFO("[WindowManager] Window {} registered (SDL id {})", handle, sdlId);
        return handle;
    }

    void WindowManager::DestroyWindow(WindowHandle handle) {
        auto it = m_Windows.find(handle);
        if (it == m_Windows.end()) return;

        u32 sdlId = static_cast<SDL3Window *>(it->second.get())->GetSDLWindowID();
        m_SDLIdToHandle.erase(sdlId);

        it->second->Shutdown();
        m_Windows.erase(it);

        if (m_PrimaryHandle == handle) {
            m_PrimaryHandle = m_Windows.empty()
                                  ? kInvalidWindow
                                  : m_Windows.begin()->first;
        }

        LOG_INFO("[WindowManager] Window {} destroyed", handle);
    }

    void WindowManager::ShutdownAll() {
        for (auto &[handle, window]: m_Windows)
            window->Shutdown();
        m_Windows.clear();
        m_SDLIdToHandle.clear();
        m_PrimaryHandle = kInvalidWindow;
    }

    IWindow *WindowManager::Get(WindowHandle handle) {
        auto it = m_Windows.find(handle);
        return it != m_Windows.end() ? it->second.get() : nullptr;
    }

    const IWindow *WindowManager::Get(WindowHandle handle) const {
        auto it = m_Windows.find(handle);
        return it != m_Windows.end() ? it->second.get() : nullptr;
    }

    bool WindowManager::IsValid(WindowHandle handle) const {
        return m_Windows.count(handle) > 0;
    }

    IWindow *WindowManager::GetPrimary() {
        return Get(m_PrimaryHandle);
    }

    void WindowManager::DispatchWindowEvent(u32 sdlWindowId, u32 eventType,
                                            u32 data1, u32 data2) {
        auto it = m_SDLIdToHandle.find(sdlWindowId);
        if (it == m_SDLIdToHandle.end()) return;

        IWindow *window = Get(it->second);
        if (!window) return;

        static_cast<SDL3Window *>(window)->OnSDLEvent(eventType, data1, data2);
    }
} // namespace Engine
