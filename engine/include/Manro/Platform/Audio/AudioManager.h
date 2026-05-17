#pragma once

#include <Manro/Interfaces/IAudioBackend.h>
#include <Manro/Core/Types.h>
#include <memory>
#include <string>

namespace Manro {
    class CAudioManager {
    public:
        CAudioManager() = default;

        ~CAudioManager() { Shutdown(); }

        CAudioManager(const CAudioManager &) = delete;

        CAudioManager &operator=(const CAudioManager &) = delete;

        bool Initialize(Scope<IAudioBackend> backend);

        void Shutdown();

        [[nodiscard]] SoundHandle LoadSound(const std::string &filepath) const;

        void UnloadSound(SoundHandle handle) const;

        void Play(SoundHandle handle, bool loop = false) const;

        void Stop(SoundHandle handle) const;

        void Pause(SoundHandle handle) const;

        void Resume(SoundHandle handle) const;

        [[nodiscard]] bool IsPlaying(SoundHandle handle) const;

        void SetVolume(SoundHandle handle, f32 volume) const;

        void SetMasterVolume(f32 volume) const;

        void PlayMusic(const std::string &filepath, bool loop = true) const;

        void StopMusic() const;

        void PauseMusic() const;

        void ResumeMusic() const;

        void SetMusicVolume(f32 volume) const;

    private:
        Scope<IAudioBackend> m_Backend;
        bool m_bInitialized{false};
    };
} // namespace Manro