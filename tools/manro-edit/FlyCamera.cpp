#include "FlyCamera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace ManroEdit {

    Manro::Vec3 FlyCamera_t::Forward() const {
        const float yR = glm::radians(Yaw);
        const float pR = glm::radians(Pitch);
        return glm::normalize(Manro::Vec3{
            cosf(pR) * cosf(yR), sinf(pR), cosf(pR) * sinf(yR)});
    }

    void FlyCamera_t::Update(const Manro::CInputManager &input, float dt) {
        using K = Manro::Key;
        auto [x, y] = input.ConsumeMouseDelta();
        Yaw += x * MouseSensitivity;
        Pitch = std::clamp(Pitch - y * MouseSensitivity, -89.f, 89.f);

        const Manro::Vec3 fwd = Forward();
        const Manro::Vec3 right = glm::normalize(glm::cross(fwd, Manro::Vec3{0, 1, 0}));
        const Manro::Vec3 up = {0, 1, 0};
        const float speed = input.IsKeyDown(K::LeftShift) ? SprintSpeed : NormalSpeed;

        Manro::Vec3 move{0};
        if (input.IsKeyDown(K::W)) move += fwd;
        if (input.IsKeyDown(K::S)) move -= fwd;
        if (input.IsKeyDown(K::D)) move += right;
        if (input.IsKeyDown(K::A)) move -= right;
        if (input.IsKeyDown(K::E)) move += up;
        if (input.IsKeyDown(K::Q)) move -= up;
        if (glm::length(move) > 0.001f)
            Position += glm::normalize(move) * speed * dt;
    }

    Manro::Mat4 FlyCamera_t::View() const {
        return glm::lookAt(Position, Position + Forward(), {0, 1, 0});
    }

    Manro::Mat4 FlyCamera_t::Projection(float fovDeg, float aspect, float nearZ, float farZ) {
        return glm::perspective(glm::radians(fovDeg), aspect, nearZ, farZ);
    }

} // namespace ManroEdit
