#pragma once

#include <Manro/Interfaces/IAudioBackend.h>
#include <unordered_map>
#include <string>

struct MIX_Audio;
struct MIX_Track;
struct MIX_Mixer;

namespace Manro {
    class CAudioBackend final : public IAudioBackend {
    public:
        CAudioBackend() = default;

        ~CAudioBackend() override { Shutdown(); }

        InitReturnVal_t Init() override;

        void Shutdown() override;

        [[nodiscard]] SoundHandle LoadSound(const std::string &filepath) override;

        void UnloadSound(SoundHandle handle) override;

        void Play(SoundHandle handle, bool loop = false) override;

        void Stop(SoundHandle handle) override;

        void Pause(SoundHandle handle) override;

        void Resume(SoundHandle handle) override;

        [[nodiscard]] bool IsPlaying(SoundHandle handle) const override;

        void SetVolume(SoundHandle handle, f32 volume) override;

        void SetMasterVolume(f32 volume) override;

        void PlayMusic(const std::string &filepath, bool loop = true) override;

        void StopMusic() override;

        void PauseMusic() override;

        void ResumeMusic() override;

        void SetMusicVolume(f32 volume) override;

    private:
        struct LoadedSound_t {
            MIX_Audio *pAudio{nullptr};
            MIX_Track *pTrack{nullptr};
        };

        std::unordered_map<SoundHandle, LoadedSound_t> m_Sounds;
        SoundHandle m_nNextHandle{1};

        MIX_Mixer *m_pMixer{nullptr};

        MIX_Audio *m_pMusicAudio{nullptr};
        MIX_Track *m_pMusicTrack{nullptr};

        bool m_bInitialized{false};
        float m_flMasterVolume{1.0f};
    };
} // namespace Manro
