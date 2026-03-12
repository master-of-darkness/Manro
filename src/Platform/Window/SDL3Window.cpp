#include <Manro/Platform/Window/SDL3Window.h>
#include <Manro/Core/Logger.h>
#include <SDL3/SDL.h>

namespace Manro {
    bool SDL3Window::Initialize(const WindowDesc &desc) {
        m_Title = desc.Title;
        m_Width = desc.Width;
        m_Height = desc.Height;

        SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
        if (desc.Resizable) flags |= SDL_WINDOW_RESIZABLE;
        if (desc.Fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

        m_Handle = SDL_CreateWindow(m_Title.c_str(),
                                    static_cast<int>(m_Width),
                                    static_cast<int>(m_Height),
                                    flags);
        if (!m_Handle) {
            LOG_ERROR("[SDL3Window] SDL_CreateWindow failed: {}", SDL_GetError());
            return false;
        }

        m_Open = true;
        m_Fullscreen = desc.Fullscreen;
        LOG_INFO("[SDL3Window] Created '{}' ({}x{})", m_Title, m_Width, m_Height);
        return true;
    }

    void SDL3Window::Shutdown() {
        if (m_Handle) {
            SDL_DestroyWindow(m_Handle);
            m_Handle = nullptr;
            m_Open = false;
            LOG_INFO("[SDL3Window] Destroyed '{}'", m_Title);
        }
    }

    void SDL3Window::SetTitle(const std::string &title) {
        m_Title = title;
        if (m_Handle) SDL_SetWindowTitle(m_Handle, title.c_str());
    }

    void SDL3Window::Resize(u32 width, u32 height) {
        m_Width = width;
        m_Height = height;
        if (m_Handle)
            SDL_SetWindowSize(m_Handle,
                              static_cast<int>(width),
                              static_cast<int>(height));
    }

    void SDL3Window::SetFullscreen(bool fullscreen) {
        if (m_Handle) {
            SDL_SetWindowFullscreen(m_Handle, fullscreen);
            m_Fullscreen = fullscreen;
        }
    }

    void *SDL3Window::GetNativeHandle() const {
        return static_cast<void *>(m_Handle);
    }

    void SDL3Window::ShowCursor(bool show) {
        if (show) SDL_ShowCursor();
        else SDL_HideCursor();
    }

    void SDL3Window::CaptureMouse(bool capture) {
        if (m_Handle)
            SDL_SetWindowRelativeMouseMode(m_Handle, capture);
    }

    u32 SDL3Window::GetSDLWindowID() const {
        return m_Handle ? SDL_GetWindowID(m_Handle) : 0;
    }

    void SDL3Window::OnSDLEvent(u32 eventType, u32 data1, u32 data2) {
        if (!m_Callback) return;

        switch (eventType) {
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                m_Open = false;
                m_Callback(WindowEvent::Close, 0, 0);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                m_Width = data1;
                m_Height = data2;
                m_Callback(WindowEvent::Resized, m_Width, m_Height);
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
