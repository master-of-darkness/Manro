#pragma once

#include <Core/Types.h>
#include <string>

namespace Engine {
    using SoundHandle = u32;
    inline constexpr SoundHandle kInvalidSound = 0;

    class IAudioBackend {
    public:
        virtual ~IAudioBackend() = default;

        virtual bool Initialize() = 0;

        virtual void Shutdown() = 0;

        virtual SoundHandle LoadSound(const std::string &filepath) = 0;

        virtual void UnloadSound(SoundHandle handle) = 0;

        virtual void Play(SoundHandle handle, bool loop = false) = 0;

        virtual void Stop(SoundHandle handle) = 0;

        virtual void Pause(SoundHandle handle) = 0;

        virtual void Resume(SoundHandle handle) = 0;

        virtual bool IsPlaying(SoundHandle handle) const = 0;

        virtual void SetVolume(SoundHandle handle, f32 volume) = 0;

        virtual void SetMasterVolume(f32 volume) = 0;

        virtual void PlayMusic(const std::string &filepath, bool loop = true) = 0;

        virtual void StopMusic() = 0;

        virtual void PauseMusic() = 0;

        virtual void ResumeMusic() = 0;

        virtual void SetMusicVolume(f32 volume) = 0;
    };
} // namespace Engine