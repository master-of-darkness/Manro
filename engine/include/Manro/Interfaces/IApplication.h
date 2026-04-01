#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Interfaces/Interface.h>

namespace Manro {

    class Renderer;

    class JobSystem;

    class InputManager;

    class IWindow;

    struct UserCmd;

    struct FrameContext {
        f32 DeltaTime{0.f};
        f32 TotalTime{0.f};
        u64 FrameIndex{0};
    };

    struct InitContext {
        IWindow &Window;
        JobSystem &Jobs;
        Renderer &Renderer;
    };

    struct RenderContext {
        Renderer &Renderer;
        const FrameContext &Frame;
    };

    class IApplication : public Interface {
    public:
        virtual ~IApplication() = default;

        virtual void OnStartup(const InitContext &ctx) = 0;

        virtual void OnShutdown() = 0;

        virtual bool OnUpdate(const FrameContext &ctx, const UserCmd &cmd) = 0;

        virtual void OnRender(FrameContext &ctx) = 0;

        virtual InputManager *GetInputManager() { return nullptr; }

        virtual struct WindowDesc GetWindowDesc() const = 0;
    };

} // namespace Manro