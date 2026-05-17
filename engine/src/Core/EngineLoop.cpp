#include <Manro/Core/EngineLoop.h>
#include <Manro/Interfaces/IApplication.h>
#include <Manro/Core/Logger.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Core/VirtualFS.h>
#include "EmbeddedShaders.h"
#include <Manro/Platform/PlatformContext.h>
#include <Manro/Platform/Window/Window.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/InputBackend.h>
#include <chrono>

#include "Profiling.h"

#ifdef _WIN32

#include <windows.h>
#include <timeapi.h>

#endif

namespace Manro {
    void CEngineLoop::Run(IApplication &app) {
        CLogger::Init();
#ifdef MANRO_PROFILING
        LOG_WARN("Profiling is enabled. This may impact performance.");
#endif

#ifdef _WIN32
        // windows shit
        timeBeginPeriod(1);
#endif

        CJobSystem jobs;
        CPlatformContext platform;
        CVirtualFS vfs;
        RegisterEmbeddedShaders(vfs);

        // Window
        auto winDesc = app.GetWindowDesc();
        WindowHandle wh = platform.GetWindowManager().AddWindow(winDesc);
        CWindow *win = platform.GetWindowManager().Get(wh);
        if (!win) {
            LOG_ERROR("[CEngineLoop] Window creation failed.");
            return;
        }

        CRenderer renderer(*win, vfs, win->GetWidth(), win->GetHeight());

        CInputManager *inputManager = app.GetInputManager();

        bool running = true;
        win->SetEventCallback([&](WindowEvent ev, u32 w, u32 h) {
            if (ev == WindowEvent::Close) running = false;
            if (ev == WindowEvent::Resized) renderer.OnResize(w, h);
        });

        InitContext_t ictx{*win, jobs, renderer, vfs};
        app.OnStartup(ictx);

        using Clock = std::chrono::steady_clock;
        auto lastTime = Clock::now();
        u64 frameIndex = 0;
        f32 totalTime = 0.f;

        while (running) {
            auto now = Clock::now();
            f32 dt = std::chrono::duration<f32>(now - lastTime).count();
            if (dt > 0.1f) dt = 0.1f;
            lastTime = now;
            totalTime += dt;

            MNR_PROFILE_VALUE("Frame Time (ms)", static_cast<f64>(dt * 1000.f));

            bool frameReady = renderer.BeginFramePace();

            {
                MNR_PROFILE_SCOPE("PollEvents");
                if (!platform.PollEvents(inputManager)) break;
            }

            if (!frameReady) continue;

            renderer.BeginFrameRecord();

            UserCmd_t cmd = inputManager ? inputManager->Poll() : UserCmd_t{};
            FrameContext_t fctx{dt, totalTime, frameIndex++};

            {
                MNR_PROFILE_SCOPE("Update");
                if (!app.OnUpdate(fctx, cmd)) break;
            }

            {
                MNR_PROFILE_SCOPE("Render");
                app.OnRender(fctx);
                renderer.EndFrameAndPresent();
            }

            MNR_PROFILE_FRAME();
        }

        app.OnShutdown();

#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }
} // namespace Manro
