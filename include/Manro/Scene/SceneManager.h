#pragma once
#include <Manro/Scene/Scene.h>
#include <Manro/Core/Types.h>
#include <Manro/Input/InputAction.h>

namespace Engine {
    class SceneManager {
    public:
        SceneManager() = default;

        ~SceneManager() = default;

        void LoadScene(Scope<Scene> scene);

        void Update(float deltaTime, const UserCmd &cmd);

        void Render(Renderer &renderer);

        Scene *GetActiveScene() const { return m_ActiveScene.get(); }

    private:
        Scope<Scene> m_ActiveScene;
    };
} // namespace Engine
