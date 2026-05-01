#include <Manro/Platform/Window/Window.h>
#include <Manro/Core/Logger.h>
#include <SDL3/SDL.h>

namespace Manro {
    bool CWindow::Initialize(const WindowDesc_t &desc) {
        m_Title = desc.Title;
        m_unWidth = desc.Width;
        m_unHeight = desc.Height;

        SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (desc.Resizable) flags |= SDL_WINDOW_RESIZABLE;
        if (desc.Fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

        m_Handle = SDL_CreateWindow(m_Title.c_str(),
                                    static_cast<int>(m_unWidth),
                                    static_cast<int>(m_unHeight),
                                    flags);
        if (!m_Handle) {
            LOG_ERROR("[Windowing] SDL_CreateWindow failed: {}", SDL_GetError());
            return false;
        }

        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(static_cast<SDL_Window *>(m_Handle), &pw, &ph);
        if (pw > 0 && ph > 0) {
            m_unWidth = static_cast<u32>(pw);
            m_unHeight = static_cast<u32>(ph);
        }

        m_bOpen = true;
        m_bFullscreen = desc.Fullscreen;
        return true;
    }

    void CWindow::Shutdown() {
        if (m_Handle) {
            SDL_DestroyWindow(static_cast<SDL_Window *>(m_Handle));
            m_Handle = nullptr;
            m_bOpen = false;
        }
    }

    void CWindow::SetTitle(const std::string &title) {
        m_Title = title;
        if (m_Handle) SDL_SetWindowTitle(static_cast<SDL_Window *>(m_Handle), title.c_str());
    }

    void CWindow::Resize(u32 width, u32 height) {
        m_unWidth = width;
        m_unHeight = height;
        if (m_Handle)
            SDL_SetWindowSize(static_cast<SDL_Window *>(m_Handle),
                              static_cast<int>(width),
                              static_cast<int>(height));
    }

    void CWindow::SetFullscreen(bool fullscreen) {
        if (m_Handle) {
            SDL_SetWindowFullscreen(static_cast<SDL_Window *>(m_Handle), fullscreen);
            m_bFullscreen = fullscreen;
        }
    }

    void *CWindow::GetNativeHandle() const {
        return m_Handle;
    }

    void CWindow::ShowCursor(bool show) {
        if (show) SDL_ShowCursor();
        else SDL_HideCursor();
    }

    void CWindow::CaptureMouse(bool capture) {
        if (m_Handle)
            SDL_SetWindowRelativeMouseMode(static_cast<SDL_Window *>(m_Handle), capture);
    }

    u32 CWindow::GetPlatformWindowID() const {
        return m_Handle ? SDL_GetWindowID(static_cast<SDL_Window *>(m_Handle)) : 0;
    }

    void CWindow::OnPlatformWindowEvent(u32 eventType, u32 data1, u32 data2) {
        if (!m_Callback) return;

        switch (eventType) {
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                m_bOpen = false;
                m_Callback(WindowEvent::Close, 0, 0);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                m_unWidth = data1;
                m_unHeight = data2;
                m_Callback(WindowEvent::Resized, m_unWidth, m_unHeight);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                m_Callback(WindowEvent::FocusGained, 0, 0);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                m_Callback(WindowEvent::FocusLost, 0, 0);
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                m_Callback(WindowEvent::Minimized, 0, 0);
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                m_Callback(WindowEvent::Restored, 0, 0);
                break;
            default:
                break;
        }
    }
} // namespace Manro