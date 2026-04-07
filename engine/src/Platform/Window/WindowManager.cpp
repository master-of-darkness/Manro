#include <Manro/Platform/Window/WindowManager.h>
#include <Manro/Platform/Window/Window.h>
#include <Manro/Core/Logger.h>

namespace Manro {
    WindowHandle WindowManager::AddWindow(const WindowDesc &desc) {
        auto window = CreateScope<Window>();
        if (!window->Initialize(desc)) {
            LOG_ERROR("[WindowManager] Failed to create window '{}'", desc.Title);
            return kInvalidWindow;
        }

        WindowHandle handle = m_NextHandle++;

        u32 platformId = static_cast<Window *>(window.get())->GetPlatformWindowID();
        m_PlatformIdToHandle[platformId] = handle;

        m_Windows.emplace(handle, std::move(window));

        if (m_PrimaryHandle == kInvalidWindow)
            m_PrimaryHandle = handle;

        LOG_INFO("[WindowManager] Window {} registered (platform id {})", handle, platformId);
        return handle;
    }

    void WindowManager::DestroyWindow(WindowHandle handle) {
        auto it = m_Windows.find(handle);
        if (it == m_Windows.end()) return;

        u32 platformId = static_cast<Window *>(it->second.get())->GetPlatformWindowID();
        m_PlatformIdToHandle.erase(platformId);

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
        m_PlatformIdToHandle.clear();
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

    void WindowManager::DispatchWindowEvent(u32 platformWindowId, u32 eventType,
                                            u32 data1, u32 data2) {
        auto it = m_PlatformIdToHandle.find(platformWindowId);
        if (it == m_PlatformIdToHandle.end()) return;

        IWindow *window = Get(it->second);
        if (!window) return;

        static_cast<Window *>(window)->OnPlatformWindowEvent(eventType, data1, data2);
    }
} // namespace Manro