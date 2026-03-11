#pragma once

#include <Manro/Core/EngineContext.h>
#include <Manro/Scene/SceneManager.h>
#include <Manro/Networking/NetworkServer.h>
#include <Manro/Networking/NetworkClient.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <Manro/Platform/Window/WindowManager.h>
#include <chrono>
#include <memory>

#include <Manro/Scene/Scene.h>
#include "Scene.h"

class Scene;
class PlayerActionMap;

enum class GameMode { Client, DedicatedServer, ListenServer };

class Game {
public:
    explicit Game(GameMode mode = GameMode::Client);

    ~Game();

    void Initialize();

    void Run();

    void Shutdown();

private:
    void UpdateLogic(float dt);

    void Render();

    Engine::Scope<Engine::Renderer> m_Renderer;
    Engine::EngineContext m_Engine;
    Engine::SceneManager m_SceneManager;

    Engine::SDL3InputBackend m_InputBackend;
    Engine::InputManager m_InputManager;


    Engine::Scope<Engine::NetworkServer> m_Server;
    Engine::Scope<Engine::NetworkClient> m_Client;

    std::unique_ptr<PlayerActionMap> m_ActionMap;

    WorldScene *m_WorldScene{nullptr};

    Engine::WindowHandle m_MainWindow{Engine::kInvalidWindow};

    GameMode m_Mode;
    bool m_IsRunning{false};

    Engine::UserCmd m_CurrentCmd{};
    std::chrono::high_resolution_clock::time_point m_LastFrameTime;
};
