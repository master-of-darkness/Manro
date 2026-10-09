#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Render/Renderer.h>

#include <cmath>

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
    inline bool BoxCollides(Manro::CRenderer &ren, const Manro::Vec3 &feet) {
        const int x0 = static_cast<int>(std::floor(feet.x - CPlayer::kHalfWidth));
        const int x1 = static_cast<int>(std::floor(feet.x + CPlayer::kHalfWidth));
        const int y0 = static_cast<int>(std::floor(feet.y));
        const int y1 = static_cast<int>(std::floor(feet.y + CPlayer::kHeight));
        const int z0 = static_cast<int>(std::floor(feet.z - CPlayer::kHalfWidth));
        const int z1 = static_cast<int>(std::floor(feet.z + CPlayer::kHalfWidth));
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                    if (ren.VoxelStreamIsSolid(Manro::Vec3(static_cast<float>(x),
                                                       static_cast<float>(y),
                                                       static_cast<float>(z))))
                        return true;
        return false;
    }

    inline bool MoveAxis(Manro::CRenderer &ren, CPlayer &p, int axis, float delta) {
        if (delta == 0.f)
            return false;
        Manro::Vec3 np = p.pos;
        if (axis == 0)
            np.x += delta;
        else if (axis == 1)
            np.y += delta;
        else
            np.z += delta;
        if (!BoxCollides(ren, np)) {
            p.pos = np;
            return false;
        }
        if (axis == 0) {
            if (delta > 0.f)
                p.pos.x = std::floor(np.x + CPlayer::kHalfWidth) - CPlayer::kHalfWidth -
                          CPlayer::kEps;
            else
                p.pos.x = std::floor(np.x - CPlayer::kHalfWidth) + 1.f + CPlayer::kHalfWidth +
                          CPlayer::kEps;
            p.vel.x = 0.f;
        } else if (axis == 2) {
            if (delta > 0.f)
                p.pos.z = std::floor(np.z + CPlayer::kHalfWidth) - CPlayer::kHalfWidth -
                          CPlayer::kEps;
            else
                p.pos.z = std::floor(np.z - CPlayer::kHalfWidth) + 1.f + CPlayer::kHalfWidth +
                          CPlayer::kEps;
            p.vel.z = 0.f;
        } else {
            if (delta > 0.f) {
                p.pos.y = std::floor(np.y + CPlayer::kHeight) - CPlayer::kHeight - CPlayer::kEps;
                if (p.vel.y > 0.f)
                    p.vel.y = 0.f;
            } else {
                p.pos.y = std::floor(np.y) + 1.f + CPlayer::kEps;
                p.vel.y = 0.f;
                p.onGround = true;
            }
        }
        return true;
    }

    struct RayHit_t {
        bool hit{false};
        int hx{0}, hy{0}, hz{0};
        int nx{0}, ny{0}, nz{0};
        float t{0.f};
    };

    inline RayHit_t RaycastVoxel(Manro::CRenderer &ren, const Manro::Vec3 &origin,
                                 const Manro::Vec3 &dir, float maxDist) {
        RayHit_t r;
        int x = static_cast<int>(std::floor(origin.x));
        int y = static_cast<int>(std::floor(origin.y));
        int z = static_cast<int>(std::floor(origin.z));
        const int stepX = (dir.x > 0.f) ? 1 : -1;
        const int stepY = (dir.y > 0.f) ? 1 : -1;
        const int stepZ = (dir.z > 0.f) ? 1 : -1;
        const float tDeltaX = (dir.x != 0.f) ? std::abs(1.f / dir.x) : 1e30f;
        const float tDeltaY = (dir.y != 0.f) ? std::abs(1.f / dir.y) : 1e30f;
        const float tDeltaZ = (dir.z != 0.f) ? std::abs(1.f / dir.z) : 1e30f;
        float tMaxX = (dir.x != 0.f)
                          ? ((stepX > 0 ? (float(x + 1) - origin.x) : (origin.x - float(x))) *
                             tDeltaX)
                          : 1e30f;
        float tMaxY = (dir.y != 0.f)
                          ? ((stepY > 0 ? (float(y + 1) - origin.y) : (origin.y - float(y))) *
                             tDeltaY)
                          : 1e30f;
        float tMaxZ = (dir.z != 0.f)
                          ? ((stepZ > 0 ? (float(z + 1) - origin.z) : (origin.z - float(z))) *
                             tDeltaZ)
                          : 1e30f;
        float t = 0.f;
        for (int i = 0; i < 256; ++i) {
            if (tMaxX < tMaxY && tMaxX < tMaxZ) {
                x += stepX;
                t = tMaxX;
                tMaxX += tDeltaX;
                r.nx = -stepX;
                r.ny = 0;
                r.nz = 0;
            } else if (tMaxY < tMaxZ) {
                y += stepY;
                t = tMaxY;
                tMaxY += tDeltaY;
                r.nx = 0;
                r.ny = -stepY;
                r.nz = 0;
            } else {
                z += stepZ;
                t = tMaxZ;
                tMaxZ += tDeltaZ;
                r.nx = 0;
                r.ny = 0;
                r.nz = -stepZ;
            }
            if (t > maxDist)
                return r;
            if (ren.VoxelStreamIsSolid(Manro::Vec3(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(z)))) {
                r.hit = true;
                r.hx = x;
                r.hy = y;
                r.hz = z;
                r.t = t;
                return r;
            }
        }
        return r;
    }
}

inline void PlayerUpdate(CPlayer &p, Manro::CRenderer &ren, Manro::CInputManager &in,
                         const Manro::Vec3 &yawFwd, const Manro::Vec3 &right,
                         const Manro::Vec3 &fullFwd, float dt, float nowSec) {
    using K = Manro::Key;
    const bool fDown = in.IsKeyDown(K::F);
    if (fDown && !p.fWasDown)
        p.fly = !p.fly;
    p.fWasDown = fDown;

    Manro::Vec3 wish{0.f};
    if (in.IsKeyDown(K::W))
        wish += yawFwd;
    if (in.IsKeyDown(K::S))
        wish -= yawFwd;
    if (in.IsKeyDown(K::D))
        wish += right;
    if (in.IsKeyDown(K::A))
        wish -= right;
    {
        const float wl = std::sqrt(wish.x * wish.x + wish.z * wish.z);
        if (wl > 1.f)
            wish = wish * (1.f / wl);
    }

    int steps = static_cast<int>(std::ceil(dt / (1.f / 120.f)));
    if (steps < 1)
        steps = 1;
    if (steps > 12)
        steps = 12;
    const float h = dt / static_cast<float>(steps);

    const bool wDown = in.IsKeyDown(K::W);
    if (wDown && !p.wWasDown && (nowSec - p.lastWRelease) < 0.30f)
        p.sprintLatch = true;
    if (!wDown) {
        if (p.wWasDown)
            p.lastWRelease = nowSec;
        p.sprintLatch = false;
    }
    p.wWasDown = wDown;
    const bool sprintKey = in.IsKeyDown(K::LeftShift) || in.IsKeyDown(K::LeftCtrl);
    const float wishLen = std::sqrt(wish.x * wish.x + wish.z * wish.z);
    p.sprinting = false;

    for (int s = 0; s < steps; ++s) {
        if (p.fly) {
            const float speed = sprintKey ? CPlayer::kFlyFast : CPlayer::kFly;
            Manro::Vec3 v{0.f};
            if (in.IsKeyDown(K::W))
                v += fullFwd;
            if (in.IsKeyDown(K::S))
                v -= fullFwd;
            if (in.IsKeyDown(K::D))
                v += right;
            if (in.IsKeyDown(K::A))
                v -= right;
            if (in.IsKeyDown(K::Space))
                v.y += 1.f;
            if (in.IsKeyDown(K::C))
                v.y -= 1.f;
            const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (len > 1e-6f)
                v = v * (speed / len);
            p.pos += v * h;
            p.vel = v;
            p.onGround = false;
            continue;
        }

        const float bodyY = p.pos.y + 0.5f;
        const bool inFluid = ren.VoxelStreamIsFluid(
            Manro::Vec3(p.pos.x, bodyY, p.pos.z));

        float speed = CPlayer::kWalk;
        if (sprintKey || p.sprintLatch)
            speed = CPlayer::kSprint;
        if (wishLen > 0.01f && (sprintKey || p.sprintLatch))
            p.sprinting = true;
        Manro::Vec3 hv = wish * speed;

        if (inFluid) {
            p.vel.x = hv.x * 0.5f;
            p.vel.z = hv.z * 0.5f;
            p.vel.y -= CPlayer::kGravity * 0.25f * h;
            if (p.vel.y < -3.5f)
                p.vel.y = -3.5f;
            if (in.IsKeyDown(K::Space))
                p.vel.y = 4.f;
        } else {
            p.vel.x = hv.x;
            p.vel.z = hv.z;
            const bool wasGrounded = p.onGround;
            const bool jumpHeld = in.IsKeyDown(K::Space);
            if (wasGrounded && !jumpHeld) {
                p.vel.y = 0.f;
            } else {
                p.vel.y -= CPlayer::kGravity * h;
                if (p.vel.y < -55.f)
                    p.vel.y = -55.f;
                if (wasGrounded && jumpHeld) {
                    p.vel.y = CPlayer::kJumpVel;
                    p.onGround = false;
                }
            }
        }

        p.onGround = false;
        const bool hitX = PlayerDetail::MoveAxis(ren, p, 0, p.vel.x * h);
        const bool hitZ = PlayerDetail::MoveAxis(ren, p, 2, p.vel.z * h);
        const bool hitY = PlayerDetail::MoveAxis(ren, p, 1, p.vel.y * h);
        if (p.vel.y <= 0.f) {
            const Manro::Vec3 probePos = p.pos - Manro::Vec3(0.f, CPlayer::kGroundProbe, 0.f);
            if (PlayerDetail::BoxCollides(ren, probePos)) {
                p.onGround = true;
                if (!hitY && p.vel.y < 0.f) {
                    float snapY = p.pos.y;
                    for (int i = 0; i < 30; ++i) {
                        const float cand = snapY - 0.001f;
                        if (cand <= p.pos.y - CPlayer::kGroundProbe - 0.0005f)
                            break;
                        if (PlayerDetail::BoxCollides(ren, Manro::Vec3(p.pos.x, cand, p.pos.z)))
                            break;
                        snapY = cand;
                    }
                    p.pos.y = snapY;
                }
                p.vel.y = 0.f;
            }
        }
        if (p.sprintLatch && (hitX || hitZ))
            p.sprintLatch = false;
    }
}
