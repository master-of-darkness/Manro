#include <Manro/Platform/Audio/AudioBackend.h>
#include <Manro/Core/Logger.h>
#include <algorithm>
#include <ranges>
#include <SDL3_mixer/SDL_mixer.h>

namespace Manro {
    InitReturnVal_t CAudioBackend::Init() {
        if (m_bInitialized) return INIT_OK;

        if (!MIX_Init()) {
            LOG_ERROR("[Audio] MIX_Init failed: {}", SDL_GetError());
            return INIT_FAILED;
        }

        m_pMixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!m_pMixer) {
            LOG_ERROR("[Audio] MIX_CreateMixerDevice failed: {}", SDL_GetError());
            MIX_Quit();
            return INIT_FAILED;
        }

        m_bInitialized = true;
        return INIT_OK;
    }

    void CAudioBackend::Shutdown() {
        if (!m_bInitialized) return;

        for (auto &sound: m_Sounds | std::views::values) {
            if (sound.pTrack) MIX_DestroyTrack(sound.pTrack);
            if (sound.pAudio) MIX_DestroyAudio(sound.pAudio);
        }
        m_Sounds.clear();

        if (m_pMusicTrack) {
            MIX_StopTrack(m_pMusicTrack, 0);
            MIX_DestroyTrack(m_pMusicTrack);
            m_pMusicTrack = nullptr;
        }
        if (m_pMusicAudio) {
            MIX_DestroyAudio(m_pMusicAudio);
            m_pMusicAudio = nullptr;
        }

        if (m_pMixer) {
            MIX_DestroyMixer(m_pMixer);
            m_pMixer = nullptr;
        }

        MIX_Quit();
        m_bInitialized = false;
    }

    SoundHandle CAudioBackend::LoadSound(const std::string &filepath) {
        if (!m_bInitialized) return kInvalidSound;

        MIX_Audio *pAudio = MIX_LoadAudio(m_pMixer, filepath.c_str(), true);
        if (!pAudio) {
            LOG_ERROR("[Audio] Failed to load '{}': {}", filepath, SDL_GetError());
            return kInvalidSound;
        }

        MIX_Track *pTrack = MIX_CreateTrack(m_pMixer);
        MIX_SetTrackAudio(pTrack, pAudio);

        SoundHandle handle = m_nNextHandle++;
        m_Sounds[handle] = {pAudio, pTrack};

        return handle;
    }

    void CAudioBackend::UnloadSound(const SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end()) return;

        if (it->second.pTrack) {
            MIX_StopTrack(it->second.pTrack, 0);
            MIX_DestroyTrack(it->second.pTrack);
        }

        if (it->second.pAudio) {
            MIX_DestroyAudio(it->second.pAudio);
        }

        m_Sounds.erase(it);
    }

    void CAudioBackend::Play(const SoundHandle handle, const bool loop) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.pTrack) return;

        MIX_SetTrackLoops(it->second.pTrack, loop ? -1 : 0);

        MIX_PlayTrack(it->second.pTrack, 0);
    }

    void CAudioBackend::Stop(SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.pTrack) return;

        MIX_StopTrack(it->second.pTrack, 0);
    }

    void CAudioBackend::Pause(const SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.pTrack) return;

        MIX_PauseTrack(it->second.pTrack);
    }

    void CAudioBackend::Resume(const SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.pTrack) return;

        MIX_ResumeTrack(it->second.pTrack);
    }

    bool CAudioBackend::IsPlaying(const SoundHandle handle) const {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.pTrack) return false;

        return MIX_TrackPlaying(it->second.pTrack);
    }

    void CAudioBackend::SetVolume(const SoundHandle handle, const f32 volume) {
        auto it = m_Sounds.find(handle);
        if (it == m_Sounds.end() || !it->second.pTrack) return;

        MIX_SetTrackGain(it->second.pTrack, std::clamp(volume, 0.f, 1.f));
    }

    void CAudioBackend::SetMasterVolume(const f32 volume) {
        if (!m_pMixer) return;

        m_flMasterVolume = std::clamp(volume, 0.f, 1.f);
        MIX_SetMixerGain(m_pMixer, m_flMasterVolume);
    }

    void CAudioBackend::PlayMusic(const std::string &filepath, bool loop) {
        if (!m_bInitialized) return;

        StopMusic();

        if (m_pMusicTrack) {
            MIX_DestroyTrack(m_pMusicTrack);
            m_pMusicTrack = nullptr;
        }
        if (m_pMusicAudio) {
            MIX_DestroyAudio(m_pMusicAudio);
            m_pMusicAudio = nullptr;
        }

        m_pMusicAudio = MIX_LoadAudio(m_pMixer, filepath.c_str(), false);
        if (!m_pMusicAudio) {
            LOG_ERROR("[Audio] Failed to load music '{}': {}", filepath, SDL_GetError());
            return;
        }

        m_pMusicTrack = MIX_CreateTrack(m_pMixer);
        MIX_SetTrackAudio(m_pMusicTrack, m_pMusicAudio);
        MIX_SetTrackLoops(m_pMusicTrack, loop ? -1 : 0);

        MIX_PlayTrack(m_pMusicTrack, 0);
    }

    void CAudioBackend::StopMusic() {
        if (m_pMusicTrack) {
            MIX_StopTrack(m_pMusicTrack, 0);
        }
    }

    void CAudioBackend::PauseMusic() {
        if (m_pMusicTrack) {
            MIX_PauseTrack(m_pMusicTrack);
        }
    }

    void CAudioBackend::ResumeMusic() {
        if (m_pMusicTrack) {
            MIX_ResumeTrack(m_pMusicTrack);
        }
    }

    void CAudioBackend::SetMusicVolume(const f32 volume) {
        if (m_pMusicTrack) {
            MIX_SetTrackGain(m_pMusicTrack, std::clamp(volume, 0.f, 1.f));
        }
    }
} // namespace Manro
