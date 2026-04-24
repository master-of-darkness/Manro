#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Input/InputManager.h>

namespace ManroEdit {

    struct FlyCamera_t {
        Manro::Vec3 Position{0.f, 150.f, 0.f};
        float Yaw{-90.f};
        float Pitch{-10.f};
        float NormalSpeed{200.f};
        float SprintSpeed{800.f};
        float MouseSensitivity{0.1f};

        Manro::Vec3 Forward() const;

        void Update(const Manro::CInputManager &input, float dt);

        Manro::Mat4 View() const;

        static Manro::Mat4 Projection(float fovDeg, float aspect, float nearZ,
                                      float farZ);
    };

} // namespace ManroEdit
