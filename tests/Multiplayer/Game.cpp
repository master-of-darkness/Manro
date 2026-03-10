#include "Game.h"
#include "Scene.h"
#include "PlayerActionMap.h"
#include "CharacterMovementController.h"
#include <Core/Logger.h>
#include <stdexcept>
#include <chrono>

Game::Game(GameMode mode)
    : m_Mode(mode),
      m_Engine(mode != GameMode::DedicatedServer) {
}

Game::~Game() {
}

void Game::Initialize() {
    const bool needsPlatform = (m_Mode != GameMode::DedicatedServer);
    LOG_INFO("[Game] Engine initialized.");

    if (needsPlatform) {
        auto &platform = m_Engine.GetPlatform();
        auto &wm = platform.GetWindowManager();

        Engine::WindowDesc desc;
        desc.Title = "Manro Engine";
        desc.Width = 1280;
        desc.Height = 720;
        m_MainWindow = wm.AddWindow(desc);
        if (m_MainWindow == Engine::kInvalidWindow)
            throw std::runtime_error("[Game] Failed to create main window.");

        auto* window = wm.Get(m_MainWindow);
        window->SetEventCallback(
            [this](Engine::WindowEvent ev, Engine::u32 w, Engine::u32 h) {
                if (ev == Engine::WindowEvent::Close)
                    m_IsRunning = false;
                else if (ev == Engine::WindowEvent::Resized && m_Renderer)
                    m_Renderer->OnResize(w, h);
            });

        m_InputManager.SetBackend(&m_InputBackend);

        window->CaptureMouse(true);
        window->ShowCursor(false);

        m_Renderer = Engine::CreateScope<Engine::Renderer>(
            *window, 1280, 720);
        LOG_INFO("[Game] Renderer initialized.");

        if (m_Mode == GameMode::Client) {
            m_Client = Engine::CreateScope<Engine::NetworkClient>();
            m_Client->Connect("127.0.0.1", 7777);
        }
    }

    if (m_Mode == GameMode::DedicatedServer || m_Mode == GameMode::ListenServer) {
        m_Server = Engine::CreateScope<Engine::NetworkServer>(7777);
        LOG_INFO("[Game] Server initialized.");
    }

    auto worldScene = Engine::CreateScope<WorldScene>(*m_Renderer);

    m_WorldScene = worldScene.get();
    m_SceneManager.LoadScene(std::move(worldScene));
    LOG_INFO("[Game] WorldScene loaded.");

    if (m_Server && m_WorldScene)
        m_WorldScene->RegisterServerCallbacks(m_Server.get());

    if (m_Client && m_WorldScene) {
        m_WorldScene->RegisterClientCallbacks(m_Client.get());
        m_Client->SetLocalPlayerEntityId(m_WorldScene->GetPlayerEntity());
    }

    if (needsPlatform) {
        m_ActionMap = std::make_unique<PlayerActionMap>(m_InputManager);
        m_InputManager.SetActionMap(m_ActionMap.get());
    }

    m_IsRunning = true;
    m_LastFrameTime = std::chrono::high_resolution_clock::now();
    LOG_INFO("[Game] Initialization complete.");
}

void Game::Run() {
    auto &platform = m_Engine.GetPlatform();

    while (m_IsRunning) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - m_LastFrameTime).count();
        m_LastFrameTime = now;
        if (dt > 0.1f) dt = 0.1f;

        if (m_Mode != GameMode::DedicatedServer) {
            if (!platform.PollEvents(&m_InputManager))
                m_IsRunning = false;
        }

        UpdateLogic(dt);

        if (m_Mode != GameMode::DedicatedServer)
            Render();
    }
}

void Game::UpdateLogic(float dt) {
    if (m_MainWindow != Engine::kInvalidWindow) {
        m_CurrentCmd = m_InputManager.Poll();

        if (m_ActionMap && m_WorldScene)
            m_WorldScene->SetViewAngles(m_ActionMap->GetYaw(),
                                        m_ActionMap->GetPitch());
    }

    m_SceneManager.Update(dt, m_CurrentCmd);

    if (m_Client) {
        auto& reg = m_SceneManager.GetActiveScene()->GetRegistry();
        m_Client->Tick(reg, &m_WorldScene->GetPhysics(), m_CurrentCmd, dt);
    }

    if (m_Server && m_WorldScene) {
        auto &registry = m_SceneManager.GetActiveScene()->GetRegistry();
        m_Server->Tick(registry,
                       m_WorldScene->GetPhysics(),
                       m_WorldScene->GetMovementController(),
                       dt);
    }

    m_Engine.GetJobSystem().WaitAll();
}

void Game::Render() {
    if (m_Renderer) {
        if (!m_Renderer->BeginFrame())
            return;
        m_Renderer->BeginRenderPass({0.01f, 0.01f, 0.033f, 1.f});
        m_SceneManager.Render(*m_Renderer);
        m_Renderer->EndRenderPass();
        m_Renderer->EndFrameAndPresent();
    }
}

void Game::Shutdown() {
    m_Renderer.reset();
    m_Server.reset();
    m_Client.reset();
    m_IsRunning = false;
}

