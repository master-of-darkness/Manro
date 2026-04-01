#include <Manro/Platform/Audio/AudioManager.h>
#include <Manro/Core/Logger.h>

namespace Manro {
    bool AudioManager::Initialize(Scope<IAudioBackend> backend) {
        if (m_Initialized) return true;
        if (!backend) {
            LOG_ERROR("[AudioManager] Null backend provided.");
            return false;
        }

        m_Backend = std::move(backend);
        if (!m_Backend->Initialize()) {
            LOG_ERROR("[AudioManager] Backend initialization failed.");
            m_Backend.reset();
            return false;
        }

        m_Initialized = true;
        LOG_INFO("[AudioManager] Ready.");
        return true;
    }

    void AudioManager::Shutdown() {
        if (!m_Initialized) return;
        m_Backend->Shutdown();
        m_Backend.reset();
        m_Initialized = false;
    }

    SoundHandle AudioManager::LoadSound(const std::string &filepath) {
        if (!m_Initialized) return kInvalidSound;
        return m_Backend->LoadSound(filepath);
    }

    void AudioManager::UnloadSound(SoundHandle handle) {
        if (m_Initialized) m_Backend->UnloadSound(handle);
    }

    void AudioManager::Play(SoundHandle handle, bool loop) {
        if (m_Initialized) m_Backend->Play(handle, loop);
    }

    void AudioManager::Stop(SoundHandle handle) {
        if (m_Initialized) m_Backend->Stop(handle);
    }

    void AudioManager::Pause(SoundHandle handle) {
        if (m_Initialized) m_Backend->Pause(handle);
    }

    void AudioManager::Resume(SoundHandle handle) {
        if (m_Initialized) m_Backend->Resume(handle);
    }

    bool AudioManager::IsPlaying(SoundHandle handle) const {
        if (!m_Initialized) return false;
        return m_Backend->IsPlaying(handle);
    }

    void AudioManager::SetVolume(SoundHandle handle, f32 volume) {
        if (m_Initialized) m_Backend->SetVolume(handle, volume);
    }

    void AudioManager::SetMasterVolume(f32 volume) {
        if (m_Initialized) m_Backend->SetMasterVolume(volume);
    }

    void AudioManager::PlayMusic(const std::string &filepath, bool loop) {
        if (m_Initialized) m_Backend->PlayMusic(filepath, loop);
    }

    void AudioManager::StopMusic() {
        if (m_Initialized) m_Backend->StopMusic();
    }

    void AudioManager::PauseMusic() {
        if (m_Initialized) m_Backend->PauseMusic();
    }

    void AudioManager::ResumeMusic() {
        if (m_Initialized) m_Backend->ResumeMusic();
    }

    void AudioManager::SetMusicVolume(f32 volume) {
        if (m_Initialized) m_Backend->SetMusicVolume(volume);
    }
} // namespace Manro
