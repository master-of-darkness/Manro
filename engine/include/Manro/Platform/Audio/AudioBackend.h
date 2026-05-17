#pragma once

#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <unordered_map>
#include <string>

struct MIX_Audio;
struct MIX_Track;
struct MIX_Mixer;

namespace Manro {
    class CAudioBackend final {
    public:
        CAudioBackend() = default;

        ~CAudioBackend() { Shutdown(); }

        bool Init();

        void Shutdown();

        [[nodiscard]] SoundHandle LoadSound(const std::string &filepath);

        void UnloadSound(SoundHandle handle);

        void Play(SoundHandle handle, bool loop = false);

        void Stop(SoundHandle handle);

        void Pause(SoundHandle handle);

        void Resume(SoundHandle handle);

        [[nodiscard]] bool IsPlaying(SoundHandle handle) const;

        void SetVolume(SoundHandle handle, f32 volume);

        void SetMasterVolume(f32 volume);

        void PlayMusic(const std::string &filepath, bool loop = true);

        void StopMusic();

        void PauseMusic();

        void ResumeMusic();

        void SetMusicVolume(f32 volume);

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
