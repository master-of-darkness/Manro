#include <ranges>
#include <Manro/Platform/Window/WindowManager.h>
#include <Manro/Core/Logger.h>

namespace Manro {
    WindowHandle CWindowManager::AddWindow(const WindowDesc_t &desc) {
        auto window = CreateScope<CWindow>();
        if (!window->Initialize(desc)) {
            LOG_ERROR("[CWindowManager] Failed to create window '{}'", desc.Title);
            return kInvalidWindow;
        }

        WindowHandle handle = m_nNextHandle++;

        u32 platformId = window->GetPlatformWindowID();
        m_PlatformIdToHandle[platformId] = handle;

        m_Windows.emplace(handle, std::move(window));

        if (m_PrimaryHandle == kInvalidWindow)
            m_PrimaryHandle = handle;

        LOG_INFO("[CWindowManager] CWindow {} registered (platform id {})", handle, platformId);
        return handle;
    }

    void CWindowManager::DestroyWindow(WindowHandle handle) {
        auto it = m_Windows.find(handle);
        if (it == m_Windows.end()) return;

        u32 platformId = it->second->GetPlatformWindowID();
        m_PlatformIdToHandle.erase(platformId);

        m_Windows.erase(it);

        if (m_PrimaryHandle == handle) {
            m_PrimaryHandle = m_Windows.empty()
                                  ? kInvalidWindow
                                  : m_Windows.begin()->first;
        }

        LOG_INFO("[CWindowManager] CWindow {} destroyed", handle);
    }

    void CWindowManager::ShutdownAll() {
        m_Windows.clear();
        m_PlatformIdToHandle.clear();
        m_PrimaryHandle = kInvalidWindow;
    }

    CWindow *CWindowManager::Get(WindowHandle handle) {
        auto it = m_Windows.find(handle);
        return it != m_Windows.end() ? it->second.get() : nullptr;
    }

    const CWindow *CWindowManager::Get(WindowHandle handle) const {
        auto it = m_Windows.find(handle);
        return it != m_Windows.end() ? it->second.get() : nullptr;
    }

    bool CWindowManager::IsValid(WindowHandle handle) const {
        return m_Windows.contains(handle);
    }

    CWindow *CWindowManager::GetPrimary() {
        return Get(m_PrimaryHandle);
    }

    void CWindowManager::DispatchWindowEvent(u32 platformWindowId, u32 eventType,
                                             u32 data1, u32 data2) {
        auto it = m_PlatformIdToHandle.find(platformWindowId);
        if (it == m_PlatformIdToHandle.end()) return;

        CWindow *window = Get(it->second);
        if (!window) return;

        window->OnPlatformWindowEvent(eventType, data1, data2);
    }
} // namespace Manro