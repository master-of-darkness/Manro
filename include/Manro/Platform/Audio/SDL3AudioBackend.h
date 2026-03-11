#pragma once

#include <Manro/Platform/Audio/IAudioBackend.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <unordered_map>
#include <string>

namespace Engine {
    class SDL3AudioBackend final : public IAudioBackend {
    public:
        SDL3AudioBackend() = default;

        ~SDL3AudioBackend() override { Shutdown(); }

        bool Initialize() override;

        void Shutdown() override;

        SoundHandle LoadSound(const std::string &filepath) override;

        void UnloadSound(SoundHandle handle) override;

        void Play(SoundHandle handle, bool loop = false) override;

        void Stop(SoundHandle handle) override;

        void Pause(SoundHandle handle) override;

        void Resume(SoundHandle handle) override;

        bool IsPlaying(SoundHandle handle) const override;

        void SetVolume(SoundHandle handle, f32 volume) override;

        void SetMasterVolume(f32 volume) override;

        void PlayMusic(const std::string &filepath, bool loop = true) override;

        void StopMusic() override;

        void PauseMusic() override;

        void ResumeMusic() override;

        void SetMusicVolume(f32 volume) override;

    private:
        struct LoadedSound {
            MIX_Audio *audio{nullptr};
            MIX_Track *track{nullptr};
        };

        std::unordered_map<SoundHandle, LoadedSound> m_Sounds;
        SoundHandle m_NextHandle{1};

        MIX_Mixer *m_Mixer{nullptr};

        MIX_Audio *m_MusicAudio{nullptr};
        MIX_Track *m_MusicTrack{nullptr};

        bool m_Initialized{false};
        float m_MasterVolume{1.0f};
    };
} // namespace Engine
