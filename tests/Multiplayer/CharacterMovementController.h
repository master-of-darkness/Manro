#pragma once

#include <Physics/IMovementController.h>
#include <Physics/PhysicsWorld.h>
#include <ECS/Registry.h>
#include <Core/Components.h>
#include <Input/InputAction.h>
#include <cmath>
#include <glm/glm.hpp>

class CharacterMovementController final : public Engine::IMovementController {
public:
    struct Settings {
        float MoveSpeed{5.f};
        float JumpImpulse{5.5f};

        Settings() = default;
    };

    explicit CharacterMovementController(Engine::Registry &registry)
        : CharacterMovementController(registry, Settings{}) {
    }

    explicit CharacterMovementController(
        Engine::Registry &registry,
        Settings s
    )
        : m_Registry(registry), m_Settings(s) {
    }

    void ProcessMovement(const Engine::MovementContext &ctx) override {
        if (!ctx.Physics || !ctx.Cmd) return;

        const Engine::UserCmd &cmd = *ctx.Cmd;
        const Engine::u32 body = ctx.BodyHandle;

        const float yR = glm::radians(cmd.ViewYaw);
        const Engine::Vec3 fwd{cosf(yR), 0.f, sinf(yR)};
        const Engine::Vec3 rt = glm::normalize(glm::cross(fwd, {0.f, 1.f, 0.f}));

        Engine::Vec3 vel = (fwd * cmd.MoveForward + rt * cmd.MoveRight) * m_Settings.MoveSpeed;

        Engine::Vec3 currentVel = ctx.Physics->GetBodyLinearVelocity(body);
        vel.y = currentVel.y;

        ctx.Physics->SetLinearVelocity(body, vel);

        const Engine::u32 entityId = ctx.Physics->GetBodyUserData(body);
        if (entityId != Engine::kInvalidBodyHandle &&
            entityId < Engine::MAX_ENTITIES) {
            Engine::Entity e = static_cast<Engine::Entity>(entityId);
            if (m_Registry.HasComponent<Engine::JumpStateComponent>(e)) {
                auto &jump = m_Registry.GetComponent<Engine::JumpStateComponent>(e);
                const bool jumpDown = cmd.GetButton(Engine::ButtonBit::Jump);
                const bool jumpRising = jumpDown && !jump.WasJumpPressed;
                if (jumpRising && ctx.Physics->IsGrounded(body))
                    ctx.Physics->ApplyLinearImpulse(body, {0.f, m_Settings.JumpImpulse, 0.f});
                jump.WasJumpPressed = jumpDown;
            }
        }
    }

private:
    Engine::Registry &m_Registry;
    Settings m_Settings;
};
