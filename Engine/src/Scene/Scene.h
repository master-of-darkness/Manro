#pragma once

#include <Core/EngineContext.h>
#include <ECS/Registry.h>
#include <Input/InputAction.h>

namespace Engine {
    class Renderer;
    class NetworkClient;

    class Scene {
    public:
        virtual ~Scene() = default;

        virtual void OnCreate() = 0;

        virtual void OnDestroy() = 0;

        virtual void OnUpdate(float deltaTime, NetworkClient *client, const UserCmd &cmd) = 0;

        virtual void OnRender(Renderer *renderer) = 0;

        Registry &GetRegistry() { return m_Registry; }

    protected:
        Registry m_Registry;
    };
} // namespace Engine