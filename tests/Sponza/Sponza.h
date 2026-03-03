#pragma once

#include <Core/EngineContext.h>
#include <Render/Renderer.h>
#include <Input/InputManager.h>
#include <Platform/Input/SDL3InputBackend.h>
#include <Platform/Window/WindowManager.h>
#include <Input/InputAction.h>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

struct FlyCamera {
    Engine::Vec3 Position{0.f, 150.f, 0.f};
    float Yaw{-90.f};
    float Pitch{-10.f};

    float NormalSpeed{200.f};
    float SprintSpeed{800.f};
    float MouseSensitivity{0.1f};

    Engine::Vec3 Forward() const;

    void Update(const Engine::InputManager &input, float dt);

    Engine::Mat4 ViewProj(float fovDeg, float aspect, float nearZ, float farZ) const;
};

class Sponza {
public:
    Sponza() = default;

    ~Sponza() { Shutdown(); }

    void Initialize();

    void Run();

    void Shutdown();

private:
    void LoadSponza();

    void Render(float dt);

    Engine::EngineContext m_Engine;
    Engine::SDL3InputBackend m_InputBackend;
    Engine::InputManager m_InputManager;

    Engine::Scope<Engine::Renderer> m_Renderer;

    Engine::WindowHandle m_Window{Engine::kInvalidWindow};
    bool m_IsRunning{false};

    std::chrono::high_resolution_clock::time_point m_LastFrameTime;

    struct SubMesh {
        Engine::u32 meshId{0};
        Engine::u32 textureId{0};
    };

    std::vector<SubMesh> m_SubMeshes; // TODO: wrap this shit into engine

    FlyCamera m_Camera;
};
