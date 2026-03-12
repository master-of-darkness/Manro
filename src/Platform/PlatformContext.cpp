#include <Manro/Platform/PlatformContext.h>
#include <Manro/Platform/Audio/SDL3AudioBackend.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Core/Logger.h>
#include <SDL3/SDL.h>

namespace Manro {
    PlatformContext::PlatformContext() {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
            LOG_ERROR("[PlatformContext] SDL_Init failed: {}", SDL_GetError());
            return;
        }

        auto audioBackend = CreateScope<SDL3AudioBackend>();
        if (!m_AudioManager.Initialize(std::move(audioBackend))) {
            LOG_WARN("[PlatformContext] Audio backend failed – continuing without audio.");
        }

        LOG_INFO("[PlatformContext] SDL3 platform initialized.");
    }

    PlatformContext::~PlatformContext() {
        m_AudioManager.Shutdown();
        m_WindowManager.ShutdownAll();
        SDL_Quit();
        LOG_INFO("[PlatformContext] SDL3 platform shut down.");
    }

    bool PlatformContext::PollEvents(InputManager *inputManager) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    return false;

                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                case SDL_EVENT_WINDOW_MINIMIZED:
                case SDL_EVENT_WINDOW_RESTORED:
                    m_WindowManager.DispatchWindowEvent(
                        event.window.windowID,
                        event.type,
                        static_cast<u32>(event.window.data1),
                        static_cast<u32>(event.window.data2));
                    break;

                default:
                    if (inputManager) {
                        inputManager->ProcessSDLEvent(event);
                    }
                    break;
            }
        }
        return true;
    }
} // namespace Manro
