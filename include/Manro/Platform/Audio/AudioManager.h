#pragma once

#include <Manro/Platform/Audio/IAudioBackend.h>
#include <Manro/Core/Types.h>
#include <memory>
#include <string>

namespace Manro {
    class AudioManager {
    public:
        AudioManager() = default;

        ~AudioManager() { Shutdown(); }

        AudioManager(const AudioManager &) = delete;

        AudioManager &operator=(const AudioManager &) = delete;

        bool Initialize(Scope<IAudioBackend> backend);

        void Shutdown();

        SoundHandle LoadSound(const std::string &filepath);

        void UnloadSound(SoundHandle handle);

        void Play(SoundHandle handle, bool loop = false);

        void Stop(SoundHandle handle);

        void Pause(SoundHandle handle);

        void Resume(SoundHandle handle);

        bool IsPlaying(SoundHandle handle) const;

        void SetVolume(SoundHandle handle, f32 volume);

        void SetMasterVolume(f32 volume);

        void PlayMusic(const std::string &filepath, bool loop = true);

        void StopMusic();

        void PauseMusic();

        void ResumeMusic();

        void SetMusicVolume(f32 volume);

    private:
        Scope<IAudioBackend> m_Backend;
        bool m_Initialized{false};
    };
} // namespace Manro
