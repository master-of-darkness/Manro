#pragma once

#include <Manro/Core/Types.h>

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

    struct RenderContext {
        Renderer &Renderer;
        IWindow &Window;
        JobSystem &Jobs;
        const FrameContext &Frame;
    };

    class IApplication {
    public:
        virtual ~IApplication() = default;

        virtual void OnStartup() = 0;

        virtual void OnShutdown() = 0;

        virtual bool OnUpdate(const FrameContext &ctx, const UserCmd &cmd) = 0;

        virtual void OnRender(RenderContext &ctx) = 0;

        virtual InputManager *GetInputManager() { return nullptr; }

        virtual struct WindowDesc GetWindowDesc() const = 0;
    };

} // namespace Manro