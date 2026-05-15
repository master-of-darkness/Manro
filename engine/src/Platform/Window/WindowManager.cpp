#include <ranges>
#include <Manro/Platform/Window/WindowManager.h>
#include <Manro/Platform/Window/Window.h>
#include <Manro/Core/Logger.h>

namespace Manro {
    WindowHandle CWindowManager::AddWindow(const WindowDesc_t &desc) {
        auto window = CreateScope<CWindow>();
        if (!window->Initialize(desc)) {
            LOG_ERROR("[CWindowManager] Failed to create window '{}'", desc.Title);
            return kInvalidWindow;
        }

        WindowHandle handle = m_nNextHandle++;

        u32 platformId = window.get()->GetPlatformWindowID();
        m_PlatformIdToHandle[platformId] = handle;

        m_Windows.emplace(handle, std::move(window));

        if (m_PrimaryHandle == kInvalidWindow)
            m_PrimaryHandle = handle;

        LOG_INFO("[CWindowManager] CWindow {} registered (platform id {})", handle, platformId);
        return handle;
    }

    WindowHandle CWindowManager::AddWindowFromExisting(IWindow *window) {
        WindowHandle handle = m_nNextHandle++;

        u32 platformId = dynamic_cast<CWindow *>(window)->GetPlatformWindowID();
        m_PlatformIdToHandle[platformId] = handle;

        // We store raw pointer and explicitly release it on shutdown without deleting it
        m_Windows.emplace(handle, Scope<IWindow>(window));

        if (m_PrimaryHandle == kInvalidWindow)
            m_PrimaryHandle = handle;

        LOG_INFO("[CWindowManager] CWindow {} registered from factory (platform id {})", handle, platformId);
        return handle;
    }

    void CWindowManager::DestroyWindow(WindowHandle handle) {
        auto it = m_Windows.find(handle);
        if (it == m_Windows.end()) return;

        u32 platformId = dynamic_cast<CWindow *>(it->second.get())->GetPlatformWindowID();
        m_PlatformIdToHandle.erase(platformId);

        // Don't call shutdown here, the global loop does it
        it->second.release();
        m_Windows.erase(it);

        if (m_PrimaryHandle == handle) {
            m_PrimaryHandle = m_Windows.empty()
                                  ? kInvalidWindow
                                  : m_Windows.begin()->first;
        }

        LOG_INFO("[CWindowManager] CWindow {} destroyed", handle);
    }

    void CWindowManager::ShutdownAll() {
        for (auto &window: m_Windows | std::views::values) {
            window.release(); // release ownership before map is cleared
        }
        m_Windows.clear();
        m_PlatformIdToHandle.clear();
        m_PrimaryHandle = kInvalidWindow;
    }

    IWindow *CWindowManager::Get(WindowHandle handle) {
        auto it = m_Windows.find(handle);
        return it != m_Windows.end() ? it->second.get() : nullptr;
    }

    const IWindow *CWindowManager::Get(WindowHandle handle) const {
        auto it = m_Windows.find(handle);
        return it != m_Windows.end() ? it->second.get() : nullptr;
    }

    bool CWindowManager::IsValid(WindowHandle handle) const {
        return m_Windows.contains(handle);
    }

    IWindow *CWindowManager::GetPrimary() {
        return Get(m_PrimaryHandle);
    }

    void CWindowManager::DispatchWindowEvent(u32 platformWindowId, u32 eventType,
                                             u32 data1, u32 data2) {
        auto it = m_PlatformIdToHandle.find(platformWindowId);
        if (it == m_PlatformIdToHandle.end()) return;

        IWindow *window = Get(it->second);
        if (!window) return;

        dynamic_cast<CWindow *>(window)->OnPlatformWindowEvent(eventType, data1, data2);
    }
} // namespace Manro