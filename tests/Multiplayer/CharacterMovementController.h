#pragma once

#include <Physics/IMovementController.h>
#include <Physics/PhysicsWorld.h>
#include <ECS/Registry.h>
#include <Core/Logger.h>

struct CharacterSettings {
    float MaxSpeed    = 8.f;
    float Acceleration = 40.f;
    float Friction     = 15.f;
    float JumpImpulse  = 7.f;
};

class CharacterMovementController final : public Engine::IMovementController {
public:
    explicit CharacterMovementController(Engine::Registry &registry,
                                         CharacterSettings settings = {})
        : m_Registry(registry), m_Settings(settings) {}

    void ProcessMovement(const Engine::MovementContext &ctx) override {
        if (!ctx.Physics || !ctx.Cmd) return;

        const Engine::PhysicsBodyHandle body = ctx.BodyHandle;
        if (body == Engine::kInvalidBodyHandle) return;

        const float yR  = glm::radians(ctx.Cmd->ViewYaw);
        const Engine::Vec3 fwd{cosf(yR), 0.f, sinf(yR)};
        const Engine::Vec3 right = glm::normalize(glm::cross(fwd, Engine::Vec3{0.f, 1.f, 0.f}));

        Engine::Vec3 wishDir{0.f};
        wishDir += fwd   * ctx.Cmd->MoveForward;
        wishDir += right * ctx.Cmd->MoveRight;
        if (glm::length(wishDir) > 0.001f)
            wishDir = glm::normalize(wishDir);

        Engine::Vec3 currentVel = ctx.Physics->GetBodyLinearVelocity(body);

        Engine::Vec3 hVel{currentVel.x, 0.f, currentVel.z};
        Engine::Vec3 targetH = wishDir * m_Settings.MaxSpeed;
        Engine::Vec3 accel   = (targetH - hVel) * m_Settings.Acceleration * ctx.DeltaTime;
        if (glm::length(wishDir) < 0.001f)
            accel = -hVel * m_Settings.Friction * ctx.DeltaTime;

        Engine::Vec3 newVel{currentVel.x + accel.x, currentVel.y, currentVel.z + accel.z};
        ctx.Physics->SetLinearVelocity(body, newVel);

        const Engine::u32 entityId = ctx.Physics->GetBodyUserData(body);
        const bool entityValid = (entityId != 0xFFFFFFFFu);

        const bool jumpPressed = (ctx.Cmd->Buttons & (1 << Engine::ButtonBit::Jump)) != 0;
        bool jumpRising = false;
        if (entityValid && m_Registry.HasComponent<Engine::JumpStateComponent>(
                static_cast<Engine::Entity>(entityId))) {
            auto &js = m_Registry.GetComponent<Engine::JumpStateComponent>(
                static_cast<Engine::Entity>(entityId));
            jumpRising   = jumpPressed && !js.WasJumpPressed;
            js.WasJumpPressed = jumpPressed;
        }

        if (jumpRising && ctx.Physics->IsGrounded(body))
            ctx.Physics->ApplyLinearImpulse(body, {0.f, m_Settings.JumpImpulse, 0.f});
    }

private:
    Engine::Registry      &m_Registry;
    CharacterSettings      m_Settings;
};