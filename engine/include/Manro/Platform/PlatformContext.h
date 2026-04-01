#pragma once

#include <Manro/Platform/Window/WindowManager.h>
#include <Manro/Platform/Audio/AudioManager.h>
#include <Manro/Core/Types.h>

namespace Manro {
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
} // namespace Manro
