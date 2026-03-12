#pragma once

#include <Manro/Core/EngineContext.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Render/TextureManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <Manro/Platform/Window/WindowManager.h>
#include <Manro/Input/InputAction.h>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

enum class SceneType {
    Sponza,
    Bistro,
};

struct FlyCamera {
    Manro::Vec3 Position{0.f, 150.f, 0.f};
    float Yaw{-90.f};
    float Pitch{-10.f};

    float NormalSpeed{200.f};
    float SprintSpeed{800.f};
    float MouseSensitivity{0.1f};

    Manro::Vec3 Forward() const;

    void Update(const Manro::InputManager &input, float dt);

    Manro::Mat4 View() const;
    Manro::Mat4 Projection(float fovDeg, float aspect, float nearZ, float farZ) const;
    Manro::Mat4 ViewProj(float fovDeg, float aspect, float nearZ, float farZ) const;
};

class Sponza {
public:
    explicit Sponza(SceneType scene = SceneType::Sponza) : m_SceneType(scene) {
    }

    ~Sponza() { Shutdown(); }

    void Initialize();

    void Run();

    void Shutdown();

private:
    void LoadScene();

    void Render(float dt);

    Manro::EngineContext m_Engine;
    Manro::SDL3InputBackend m_InputBackend;
    Manro::InputManager m_InputManager;

    Manro::Scope<Manro::Renderer> m_Renderer;

    Manro::WindowHandle m_Window{Manro::kInvalidWindow};
    bool m_IsRunning{false};

    std::chrono::high_resolution_clock::time_point m_LastFrameTime;

    struct SubMesh {
        Manro::MeshHandle meshId;
        Manro::Scope<Manro::MaterialInstance> material;
    };

    std::vector<SubMesh> m_SubMeshes; // TODO: wrap this shit into engine

    SceneType m_SceneType{SceneType::Bistro};
    FlyCamera m_Camera;
};
