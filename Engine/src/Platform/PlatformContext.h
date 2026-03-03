#pragma once

#include <Platform/Window/WindowManager.h>
#include <Platform/Audio/AudioManager.h>
#include <Core/Types.h>

namespace Engine {
    class InputManager;
    class SDL3InputBackend;

    class PlatformContext {
    public:
        PlatformContext();

        ~PlatformContext();

        PlatformContext(const PlatformContext &) = delete;

        PlatformContext &operator=(const PlatformContext &) = delete;

        bool Initialize();

        void Shutdown();

        bool PollEvents(InputManager *inputManager = nullptr);

        WindowManager &GetWindowManager() { return m_WindowManager; }
        AudioManager &GetAudioManager() { return m_AudioManager; }

        bool IsInitialized() const { return m_Initialized; }

    private:
        WindowManager m_WindowManager;
        AudioManager m_AudioManager;
        SDL3InputBackend *m_InputBackend{nullptr};

        bool m_Initialized{false};
    };
} // namespace Engine