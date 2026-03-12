#pragma once

#include <Manro/Core/EngineContext.h>
#include <Manro/ECS/Registry.h>
#include <Manro/Input/InputAction.h>

namespace Manro {
    class Renderer;

    class Scene {
    public:
        virtual ~Scene() = default;

        virtual void OnCreate() = 0;

        virtual void OnDestroy() = 0;

        virtual void OnUpdate(float deltaTime, const UserCmd &cmd) = 0;

        virtual void OnRender(Renderer &renderer) = 0;

        Registry &GetRegistry() { return m_Registry; }

    protected:
        Registry m_Registry;
    };
} // namespace Manro
