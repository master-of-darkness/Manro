#include <Manro/Platform/Audio/SDL3AudioBackend.h>
#include <Manro/Core/Logger.h>
#include <algorithm>

namespace Engine {
    bool SDL3AudioBackend::Initialize() {
        if (m_Initialized) return true;

        if (!MIX_Init()) {
            LOG_ERROR("[SDL3AudioBackend] MIX_Init failed: {}", SDL_GetError());
            return false;
        }

        m_Mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!m_Mixer) {
            LOG_ERROR("[SDL3AudioBackend] MIX_CreateMixerDevice failed: {}", SDL_GetError());
            MIX_Quit();
            return false;
        }

        m_Initialized = true;
        LOG_INFO("[SDL3AudioBackend] Initialized SDL3_mixer device.");
        return true;
    }

    void SDL3AudioBackend::Shutdown() {
        if (!m_Initialized) return;

        for (auto &[handle, sound]: m_Sounds) {
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
        LOG_INFO("[SDL3AudioBackend] Shut down.");
    }

    SoundHandle SDL3AudioBackend::LoadSound(const std::string &filepath) {
        if (!m_Initialized) return 0; // 0 is kInvalidSound

        MIX_Audio *audio = MIX_LoadAudio(m_Mixer, filepath.c_str(), true);
        if (!audio) {
            LOG_ERROR("[SDL3AudioBackend] Failed to load '{}': {}", filepath, SDL_GetError());
            return 0;
        }

        MIX_Track *track = MIX_CreateTrack(m_Mixer);
        MIX_SetTrackAudio(track, audio);

        SoundHandle handle = m_NextHandle++;
        m_Sounds[handle] = {audio, track};

        LOG_INFO("[SDL3AudioBackend] Loaded sound '{}' -> handle {}", filepath, handle);
        return handle;
    }

    void SDL3AudioBackend::UnloadSound(SoundHandle handle) {
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

    void SDL3AudioBackend::Play(SoundHandle handle, bool loop) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_SetTrackLoops(it->second.track, loop ? -1 : 0);

        MIX_PlayTrack(it->second.track, 0);
    }

    void SDL3AudioBackend::Stop(SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_StopTrack(it->second.track, 0);
    }

    void SDL3AudioBackend::Pause(SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_PauseTrack(it->second.track);
    }

    void SDL3AudioBackend::Resume(SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_ResumeTrack(it->second.track);
    }

    bool SDL3AudioBackend::IsPlaying(SoundHandle handle) const {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return false;

        return MIX_TrackPlaying(it->second.track);
    }

    void SDL3AudioBackend::SetVolume(SoundHandle handle, f32 volume) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.track) return;

        MIX_SetTrackGain(it->second.track, std::clamp(volume, 0.f, 1.f));
    }

    void SDL3AudioBackend::SetMasterVolume(f32 volume) {
        if (!m_Mixer) return;

        m_MasterVolume = std::clamp(volume, 0.f, 1.f);
        MIX_SetMixerGain(m_Mixer, m_MasterVolume);
    }

    void SDL3AudioBackend::PlayMusic(const std::string &filepath, bool loop) {
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
            LOG_ERROR("[SDL3AudioBackend] Failed to load music '{}': {}", filepath, SDL_GetError());
            return;
        }

        m_MusicTrack = MIX_CreateTrack(m_Mixer);
        MIX_SetTrackAudio(m_MusicTrack, m_MusicAudio);
        MIX_SetTrackLoops(m_MusicTrack, loop ? -1 : 0);

        MIX_PlayTrack(m_MusicTrack, 0);
        LOG_INFO("[SDL3AudioBackend] Playing music '{}'", filepath);
    }

    void SDL3AudioBackend::StopMusic() {
        if (m_MusicTrack) {
            MIX_StopTrack(m_MusicTrack, 0);
        }
    }

    void SDL3AudioBackend::PauseMusic() {
        if (m_MusicTrack) {
            MIX_PauseTrack(m_MusicTrack);
        }
    }

    void SDL3AudioBackend::ResumeMusic() {
        if (m_MusicTrack) {
            MIX_ResumeTrack(m_MusicTrack);
        }
    }

    void SDL3AudioBackend::SetMusicVolume(f32 volume) {
        if (m_MusicTrack) {
            MIX_SetTrackGain(m_MusicTrack, std::clamp(volume, 0.f, 1.f));
        }
    }
} // namespace Engine
