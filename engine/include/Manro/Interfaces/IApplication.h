#pragma once

#include <Manro/Core/Types.h>

namespace Manro {
    class CRenderer;
    class CJobSystem;
    class CInputManager;
    class CVirtualFS;
    class CWindow;
    class CWorld;

    struct UserCmd_t;

    struct FrameContext_t {
        f32 DeltaTime{0.f};
        f32 TotalTime{0.f};
        u64 FrameIndex{0};
    };

    struct InitContext_t {
        CWindow &CWindow;
        CJobSystem &Jobs;
        CRenderer &CRenderer;
        CVirtualFS &Vfs;
        CWorld &World;
    };

    struct RenderContext_t {
        CRenderer &CRenderer;
        const FrameContext_t &Frame;
    };

    struct WindowDesc_t;

    class IApplication {
    public:
        virtual ~IApplication() = default;

        virtual void OnStartup(const InitContext_t &ctx) = 0;
        virtual void OnShutdown() = 0;
        virtual bool OnUpdate(const FrameContext_t &ctx, const UserCmd_t &cmd) = 0;
        virtual void OnRender(FrameContext_t &ctx) = 0;
        virtual CInputManager *GetInputManager() { return nullptr; }
        virtual WindowDesc_t GetWindowDesc() const = 0;
    };
} // namespace Manro
