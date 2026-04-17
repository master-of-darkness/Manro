#include <Manro/Platform/Audio/AudioBackend.h>
#include <Manro/Core/Logger.h>
#include <algorithm>
#include <ranges>
#include <SDL3_mixer/SDL_mixer.h>

namespace Manro {
    bool AudioBackend::Initialize() {
        if (m_Initialized) return true;

        if (!MIX_Init()) {
            LOG_ERROR("[Audio] MIX_Init failed: {}", SDL_GetError());
            return false;
        }

        m_Mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!m_Mixer) {
            LOG_ERROR("[Audio] MIX_CreateMixerDevice failed: {}", SDL_GetError());
            MIX_Quit();
            return false;
        }

        m_Initialized = true;
        return true;
    }

    void AudioBackend::Shutdown() {
        if (!m_Initialized) return;

        for (auto &sound: m_Sounds | std::views::values) {
            if (sound.track) MIX_DestroyTrack(sound.track);
            if (sound.audio) MIX_DestroyAudio(sound.audio);
        }
        m_Sounds.clear();

        if (m_MusicTrack) {
            MIX_StopTrack(m_MusicTrack, 0);
            MIX_DestroyTrack(m_MusicTrack);
            m_MusicTrack = nullptr;
        }
        if (m_MusicAudio) {
            MIX_DestroyAudio(m_MusicAudio);
            m_MusicAudio = nullptr;
        }

        if (m_Mixer) {
            MIX_DestroyMixer(m_Mixer);
            m_Mixer = nullptr;
        }

        MIX_Quit();
        m_Initialized = false;
    }

    SoundHandle AudioBackend::LoadSound(const std::string &filepath) {
        if (!m_Initialized) return kInvalidSound;

        MIX_Audio *audio = MIX_LoadAudio(m_Mixer, filepath.c_str(), true);
        if (!audio) {
            LOG_ERROR("[Audio] Failed to load '{}': {}", filepath, SDL_GetError());
            return kInvalidSound;
        }

        MIX_Track *track = MIX_CreateTrack(m_Mixer);
        MIX_SetTrackAudio(track, audio);

        SoundHandle handle = m_NextHandle++;
        m_Sounds[handle] = {audio, track};

        return handle;
    }

    void AudioBackend::UnloadSound(const SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end()) return;

        if (it->second.track) {
            MIX_StopTrack(it->second.track, 0);
            MIX_DestroyTrack(it->second.track);
        }

        if (it->second.audio) {
            MIX_DestroyAudio(it->second.audio);
        }

        m_Sounds.erase(it);
    }

    void AudioBackend::Play(const SoundHandle handle, const bool loop) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_SetTrackLoops(it->second.track, loop ? -1 : 0);

        MIX_PlayTrack(it->second.track, 0);
    }

    void AudioBackend::Stop(SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_StopTrack(it->second.track, 0);
    }

    void AudioBackend::Pause(const SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_PauseTrack(it->second.track);
    }

    void AudioBackend::Resume(const SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_ResumeTrack(it->second.track);
    }

    bool AudioBackend::IsPlaying(const SoundHandle handle) const {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return false;

        return MIX_TrackPlaying(it->second.track);
    }

    void AudioBackend::SetVolume(const SoundHandle handle, const f32 volume) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_SetTrackGain(it->second.track, std::clamp(volume, 0.f, 1.f));
    }

    void AudioBackend::SetMasterVolume(const f32 volume) {
        if (!m_Mixer) return;

        m_MasterVolume = std::clamp(volume, 0.f, 1.f);
        MIX_SetMixerGain(m_Mixer, m_MasterVolume);
    }

    void AudioBackend::PlayMusic(const std::string &filepath, bool loop) {
        if (!m_Initialized) return;

        StopMusic();

        if (m_MusicTrack) {
            MIX_DestroyTrack(m_MusicTrack);
            m_MusicTrack = nullptr;
        }
        if (m_MusicAudio) {
            MIX_DestroyAudio(m_MusicAudio);
            m_MusicAudio = nullptr;
        }

        m_MusicAudio = MIX_LoadAudio(m_Mixer, filepath.c_str(), false);
        if (!m_MusicAudio) {
            LOG_ERROR("[Audio] Failed to load music '{}': {}", filepath, SDL_GetError());
            return;
        }

        m_MusicTrack = MIX_CreateTrack(m_Mixer);
        MIX_SetTrackAudio(m_MusicTrack, m_MusicAudio);
        MIX_SetTrackLoops(m_MusicTrack, loop ? -1 : 0);

        MIX_PlayTrack(m_MusicTrack, 0);
    }

    void AudioBackend::StopMusic() {
        if (m_MusicTrack) {
            MIX_StopTrack(m_MusicTrack, 0);
        }
    }

    void AudioBackend::PauseMusic() {
        if (m_MusicTrack) {
            MIX_PauseTrack(m_MusicTrack);
        }
    }

    void AudioBackend::ResumeMusic() {
        if (m_MusicTrack) {
            MIX_ResumeTrack(m_MusicTrack);
        }
    }

    void AudioBackend::SetMusicVolume(const f32 volume) {
        if (m_MusicTrack) {
            MIX_SetTrackGain(m_MusicTrack, std::clamp(volume, 0.f, 1.f));
        }
    }
} // namespace Manro