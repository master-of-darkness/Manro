#include <Manro/Core/EngineLoop.h>
#include <Manro/Core/IApplication.h>
#include <Manro/Core/EngineContext.h>
#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <chrono>

namespace Manro {

    void EngineLoop::Run(IApplication &app) {
        EngineContext engine;
        RegisterEmbeddedShaders();

        auto winDesc = app.GetWindowDesc();
        auto &wm = engine.GetPlatform().GetWindowManager();
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

        InitContext ictx{*win, engine.GetJobSystem(), renderer};
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

            if (!engine.GetPlatform().PollEvents(inputManager)) break;

            UserCmd cmd = inputManager->Poll();
            FrameContext fctx{dt, totalTime, frameIndex++};

            if (!app.OnUpdate(fctx, cmd)) break;
            if (!renderer.BeginFrame()) continue;

            RenderContext rctx{renderer, fctx};
            app.OnRender(rctx);

            renderer.EndFrameAndPresent();
        }

        app.OnShutdown();
    }

} // namespace Manro