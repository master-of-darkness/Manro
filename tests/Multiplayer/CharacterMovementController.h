#pragma once

#include <Manro/Physics/IMovementController.h>
#include <Manro/Physics/PhysicsWorld.h>
#include <Manro/ECS/Registry.h>
#include <Manro/Core/Components.h>
#include <Manro/Core/Logger.h>

struct CharacterSettings {
    float MaxSpeed = 8.f;
    float Acceleration = 40.f;
    float Friction = 15.f;
    float JumpImpulse = 7.f;
};

class CharacterMovementController final : public Manro::IMovementController {
public:
    explicit CharacterMovementController(Manro::Registry &registry,
                                         CharacterSettings settings = {})
        : m_Registry(registry), m_Settings(settings) {
    }

    void ProcessMovement(const Manro::MovementContext &ctx) override {
        if (!ctx.Physics || !ctx.Cmd) return;

        const Manro::PhysicsBodyHandle body = ctx.BodyHandle;
        if (body == Manro::kInvalidBodyHandle) return;

        const float yR = glm::radians(ctx.Cmd->ViewYaw);
        const Manro::Vec3 fwd{cosf(yR), 0.f, sinf(yR)};
        const Manro::Vec3 right = glm::normalize(glm::cross(fwd, Manro::Vec3{0.f, 1.f, 0.f}));

        Manro::Vec3 wishDir{0.f};
        wishDir += fwd * ctx.Cmd->MoveForward;
        wishDir += right * ctx.Cmd->MoveRight;
        if (glm::length(wishDir) > 0.001f)
            wishDir = glm::normalize(wishDir);

        Manro::Vec3 currentVel = ctx.Physics->GetBodyLinearVelocity(body);

        Manro::Vec3 hVel{currentVel.x, 0.f, currentVel.z};
        Manro::Vec3 targetH = wishDir * m_Settings.MaxSpeed;
        Manro::Vec3 accel = (targetH - hVel) * m_Settings.Acceleration * ctx.DeltaTime;
        if (glm::length(wishDir) < 0.001f)
            accel = -hVel * m_Settings.Friction * ctx.DeltaTime;

        Manro::Vec3 newVel{currentVel.x + accel.x, currentVel.y, currentVel.z + accel.z};
        ctx.Physics->SetLinearVelocity(body, newVel);

        const Manro::u32 entityId = ctx.Physics->GetBodyUserData(body);
        const bool entityValid = (entityId != 0xFFFFFFFFu);

        const bool jumpPressed = (ctx.Cmd->Buttons & (1 << Manro::ButtonBit::Jump)) != 0;
        bool jumpRising = false;
        if (entityValid && m_Registry.HasComponent<Manro::JumpStateComponent>(
                static_cast<Manro::Entity>(entityId))) {
            auto &js = m_Registry.GetComponent<Manro::JumpStateComponent>(
                static_cast<Manro::Entity>(entityId));
            jumpRising = jumpPressed && !js.WasJumpPressed;
            js.WasJumpPressed = jumpPressed;
        }

        if (jumpRising && ctx.Physics->IsGrounded(body))
            ctx.Physics->ApplyLinearImpulse(body, {0.f, m_Settings.JumpImpulse, 0.f});
    }

private:
    Manro::Registry &m_Registry;
    CharacterSettings m_Settings;
};
