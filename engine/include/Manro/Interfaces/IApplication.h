#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Interfaces/IAppSystem.h>

namespace Manro {
    class CRenderer;

    class CJobSystem;

    class CInputManager;

    class IWindow;

    struct UserCmd_t;

    struct FrameContext_t {
        f32 DeltaTime{0.f};
        f32 TotalTime{0.f};
        u64 FrameIndex{0};
    };

    struct InitContext_t {
        IWindow &CWindow;
        CJobSystem &Jobs;
        CRenderer &CRenderer;
    };

    struct RenderContext_t {
        CRenderer &CRenderer;
        const FrameContext_t &Frame;
    };

    class IApplication : public IAppSystem {
    public:
        ~IApplication() override = default;

        virtual void OnStartup(const InitContext_t &ctx) = 0;

        virtual void OnShutdown() = 0;

        virtual bool OnUpdate(const FrameContext_t &ctx, const UserCmd_t &cmd) = 0;

        virtual void OnRender(FrameContext_t &ctx) = 0;

        virtual CInputManager *GetInputManager() { return nullptr; }

        virtual struct WindowDesc_t GetWindowDesc() const = 0;
    };
} // namespace Manro