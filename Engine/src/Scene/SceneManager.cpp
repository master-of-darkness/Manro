#include "SceneManager.h"

namespace Engine {
    void SceneManager::LoadScene(Scope<Scene> scene) {
        if (m_ActiveScene)
            m_ActiveScene->OnDestroy();

        m_ActiveScene = std::move(scene);

        if (m_ActiveScene)
            m_ActiveScene->OnCreate();
    }

    void SceneManager::Update(float deltaTime, const UserCmd &cmd) {
        if (m_ActiveScene)
            m_ActiveScene->OnUpdate(deltaTime, cmd);
    }

    void SceneManager::Render(Renderer &renderer) {
        if (m_ActiveScene)
            m_ActiveScene->OnRender(renderer);
    }
} // namespace Engine