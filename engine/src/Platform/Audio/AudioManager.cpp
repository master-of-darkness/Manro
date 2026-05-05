#include <Manro/Platform/Audio/AudioManager.h>
#include <Manro/Core/Logger.h>

namespace Manro {
    bool CAudioManager::Initialize(Scope<IAudioBackend> backend) {
        if (m_bInitialized) return true;
        if (!backend) {
            LOG_ERROR("[Audio] Null backend provided.");
            return false;
        }

        m_Backend = std::move(backend);

        m_bInitialized = true;
        return true;
    }

    void CAudioManager::Shutdown() {
        if (!m_bInitialized) return;
        m_Backend.release(); // release ownership so we don't double free
        m_bInitialized = false;
    }

    SoundHandle CAudioManager::LoadSound(const std::string &filepath) {
        if (!m_bInitialized) return kInvalidSound;
        return m_Backend->LoadSound(filepath);
    }

    void CAudioManager::UnloadSound(SoundHandle handle) {
        if (m_bInitialized) m_Backend->UnloadSound(handle);
    }

    void CAudioManager::Play(SoundHandle handle, bool loop) {
        if (m_bInitialized) m_Backend->Play(handle, loop);
    }

    void CAudioManager::Stop(SoundHandle handle) {
        if (m_bInitialized) m_Backend->Stop(handle);
    }

    void CAudioManager::Pause(SoundHandle handle) {
        if (m_bInitialized) m_Backend->Pause(handle);
    }

    void CAudioManager::Resume(SoundHandle handle) {
        if (m_bInitialized) m_Backend->Resume(handle);
    }

    bool CAudioManager::IsPlaying(SoundHandle handle) const {
        if (!m_bInitialized) return false;
        return m_Backend->IsPlaying(handle);
    }

    void CAudioManager::SetVolume(SoundHandle handle, f32 volume) {
        if (m_bInitialized) m_Backend->SetVolume(handle, volume);
    }

    void CAudioManager::SetMasterVolume(f32 volume) {
        if (m_bInitialized) m_Backend->SetMasterVolume(volume);
    }

    void CAudioManager::PlayMusic(const std::string &filepath, bool loop) {
        if (m_bInitialized) m_Backend->PlayMusic(filepath, loop);
    }

    void CAudioManager::StopMusic() {
        if (m_bInitialized) m_Backend->StopMusic();
    }

    void CAudioManager::PauseMusic() {
        if (m_bInitialized) m_Backend->PauseMusic();
    }

    void CAudioManager::ResumeMusic() {
        if (m_bInitialized) m_Backend->ResumeMusic();
    }

    void CAudioManager::SetMusicVolume(f32 volume) {
        if (m_bInitialized) m_Backend->SetMusicVolume(volume);
    }
} // namespace Manro