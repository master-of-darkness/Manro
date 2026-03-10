#pragma once

#include <Platform/Window/WindowManager.h>
#include <Platform/Audio/AudioManager.h>
#include <Core/Types.h>

namespace Engine {
    class InputManager;

    class PlatformContext {
    public:
        PlatformContext();

        ~PlatformContext();

        PlatformContext(const PlatformContext &) = delete;
        PlatformContext &operator=(const PlatformContext &) = delete;

        bool PollEvents(InputManager *inputManager = nullptr);

        WindowManager &GetWindowManager() { return m_WindowManager; }
        AudioManager &GetAudioManager() { return m_AudioManager; }

    private:
        WindowManager m_WindowManager;
        AudioManager m_AudioManager;
    };
} // namespace Engine