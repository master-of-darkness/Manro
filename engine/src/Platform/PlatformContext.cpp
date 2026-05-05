#include <Manro/Platform/PlatformContext.h>
#include <Manro/Platform/Audio/AudioBackend.h>
#include <Manro/Platform/Input/InputBackend.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/PlatformEvent.h>
#include <Manro/Core/InterfaceReg.h>
#include <Manro/Core/Logger.h>
#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>

namespace Manro {
    extern void ForceLinkAudioBackend();

    extern void ForceLinkInputBackend();

    extern void ForceLinkWindow();

    CPlatformContext::CPlatformContext() {
        ForceLinkAudioBackend();
        ForceLinkInputBackend();
        ForceLinkWindow();

        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
            LOG_ERROR("[CPlatformContext] SDL_Init failed: {}", SDL_GetError());
            return;
        }

        int dummy;
        auto *audioBackend = static_cast<CAudioBackend *>(Sys_GetFactory("IAUDIOBACKEND_001", &dummy));
        if (audioBackend) {
            m_AudioManager.Initialize(Scope<IAudioBackend>(audioBackend));
        } else {
            LOG_WARN("[CPlatformContext] Audio backend failed to get from factory.");
        }
    }

    CPlatformContext::~CPlatformContext() {
        m_AudioManager.Shutdown();
        m_WindowManager.ShutdownAll();
        SDL_Quit();
    }

    bool CPlatformContext::PollEvents(const CInputManager *inputManager) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            bool passToImGui = true;
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                SDL_Window *window = SDL_GetWindowFromID(event.motion.windowID);
                if (window && SDL_GetWindowRelativeMouseMode(window)) {
                    passToImGui = false;
                }
            }

            if (passToImGui) {
                ImGui_ImplSDL3_ProcessEvent(&event);
            }
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    return false;

                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
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
                        inputManager->ProcessEvent(PlatformEvent_t{&event});
                    }
                    break;
            }
        }
        return true;
    }
} // namespace Manro