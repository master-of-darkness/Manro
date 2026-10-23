#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Input/InputManager.h>

namespace Manro {
    class CVoxelRuntime;
}

struct CPlayer {
    Manro::Vec3 pos{0.f};
    Manro::Vec3 vel{0.f};
    bool onGround{false};
    bool fly{false};
    bool fWasDown{false};
    bool sprinting{false};
    bool sprintLatch{false};
    bool wWasDown{false};
    float lastWRelease{-10.f};

    static constexpr float kHalfWidth = 0.3f;
    static constexpr float kHeight = 1.8f;
    static constexpr float kEye = 1.62f;
    static constexpr float kGravity = 28.f;
    static constexpr float kJumpVel = 9.f;
    static constexpr float kGroundProbe = 0.03f;
    static constexpr float kWalk = 4.3f;
    static constexpr float kSprint = 5.7f;
    static constexpr float kFly = 15.f;
    static constexpr float kFlyFast = 40.f;
    static constexpr float kEps = 0.002f;
};

namespace PlayerDetail {
    struct RayHit_t {
        bool hit{false};
        int hx{0}, hy{0}, hz{0};
        int nx{0}, ny{0}, nz{0};
        float t{0.f};
    };

    RayHit_t RaycastVoxel(Manro::CVoxelRuntime &runtime, const Manro::Vec3 &origin,
                          const Manro::Vec3 &dir, float maxDist);
}

void PlayerUpdate(CPlayer &p, Manro::CVoxelRuntime &runtime, Manro::CInputManager &in,
                  const Manro::Vec3 &yawFwd, const Manro::Vec3 &right,
                  const Manro::Vec3 &fullFwd, float dt, float nowSec);
