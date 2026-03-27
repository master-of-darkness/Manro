#include <Manro/Core/EngineLoop.h>
#include <Manro/Core/IApplication.h>
#include <Manro/Core/Logger.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Platform/PlatformContext.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <chrono>

#ifdef _WIN32

#include <windows.h>
#include <timeapi.h>

#endif

namespace Manro {

    void EngineLoop::Run(IApplication &app) {
        Logger::Init();

#ifdef _WIN32
        timeBeginPeriod(1);
#endif

        JobSystem jobs;
        PlatformContext platform;

        RegisterEmbeddedShaders();

        auto winDesc = app.GetWindowDesc();
        auto &wm = platform.GetWindowManager();
        WindowHandle wh = wm.AddWindow(winDesc);
        IWindow *win = wm.Get(wh);

        Renderer renderer(*win, winDesc.Width, winDesc.Height);

        SDL3InputBackend fallbackInputBackend;
        InputManager fallbackInputManager;
        fallbackInputManager.SetBackend(&fallbackInputBackend);

        InputManager *inputManager = app.GetInputManager();
        if (!inputManager)
            inputManager = &fallbackInputManager;

        bool running = true;
        win->SetEventCallback([&](WindowEvent ev, u32 w, u32 h) {
            if (ev == WindowEvent::Close) running = false;
            if (ev == WindowEvent::Resized) renderer.OnResize(w, h);
        });

        InitContext ictx{*win, jobs, renderer};
        app.OnStartup(ictx);

        using Clock = std::chrono::high_resolution_clock;
        auto lastTime = Clock::now();
        u64 frameIndex = 0;
        f32 totalTime = 0.f;

        while (running) {
            auto now = Clock::now();
            f32 dt = std::chrono::duration<f32>(now - lastTime).count();
            if (dt > 0.1f) dt = 0.1f;
            lastTime = now;
            totalTime += dt;

            if (!platform.PollEvents(inputManager)) break;

            UserCmd cmd = inputManager->Poll();
            FrameContext fctx{dt, totalTime, frameIndex++};

            if (!app.OnUpdate(fctx, cmd)) break;
            if (!renderer.BeginFrame()) continue;

            app.OnRender(fctx);

            renderer.EndFrameAndPresent();
        }

        app.OnShutdown();

#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }

} // namespace Manro