#pragma once

#include <Manro/Platform/Window/WindowManager.h>
#include <Manro/Platform/Audio/AudioManager.h>
#include <Manro/Core/Types.h>

namespace Manro {
    class CInputManager;

    class CPlatformContext {
    public:
        // Initializes SDL and constructs the audio backend. Window creation is
        // performed via GetWindowManager().AddWindow(...) by the caller.
        CPlatformContext();

        ~CPlatformContext();

        CPlatformContext(const CPlatformContext &) = delete;

        CPlatformContext &operator=(const CPlatformContext &) = delete;

        bool PollEvents(const CInputManager *inputManager = nullptr);

        CWindowManager &GetWindowManager() { return m_WindowManager; }

        CAudioManager &GetAudioManager() { return m_AudioManager; }

    private:
        CWindowManager m_WindowManager;
        CAudioManager m_AudioManager;
    };
} // namespace Manro
