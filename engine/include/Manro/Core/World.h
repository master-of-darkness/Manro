#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Handles.h>

#include <flecs.h>

namespace Manro
{
    struct Position {
        float x, y, z;
    };

    struct Rotation {
        float x, y, z, w;
    };

    struct Scale {
        float x, y, z;
    };

    struct LocalToWorld {
        Mat4 value;
    };

    struct PhysicsBody {
        PhysicsBodyHandle handle{kInvalidBodyHandle};
    };

    struct Collider {
        ColliderShape_e shape{ColliderShape_e::None};
        Vec3 offset{0.f};
        Vec3 halfExtents{50.f};
    };

    struct RigidBody {
        PhysicsBodyType type{PhysicsBodyType::Static};
        Vec3 halfExtents{0.5f, 0.5f, 0.5f};
        float capsuleRadius{0.3f};
        float capsuleHalfHeight{0.5f};
        float mass{1.f};
        float friction{0.2f};
        float restitution{0.f};
        bool allowSleeping{false};
        bool lockRotation{false};
    };

    struct Light {
        int type{0};
        Vec3 position{0.f};
        Vec3 direction{0.f, -1.f, 0.f};
        Vec3 color{1.f};
        float intensity{1.f};
        float range{1000.f};
    };

    struct Name {
        const char *value;
    };

    class CModel;

    struct ModelRef {
        CModel *ptr{nullptr};
    };

    class CWorld {
    public:
        CWorld();
        ~CWorld();

        CWorld(const CWorld &) = delete;
        CWorld &operator=(const CWorld &) = delete;

        flecs::world &GetWorld() { return m_World; }
        const flecs::world &GetWorld() const { return m_World; }

        flecs::entity CreateEntity(const char *name = nullptr);
        void DestroyEntity(flecs::entity e);
        bool IsValid(flecs::entity e) const;

        template<typename T>
        flecs::entity Set(flecs::entity e, T &&comp) {
            e.set<T>(std::forward<T>(comp));
            return e;
        }

        template<typename T>
        flecs::entity Set(flecs::entity e, const T &comp) {
            e.set<T>(comp);
            return e;
        }

        template<typename T>
        T *Get(flecs::entity e) {
            return &e.get_mut<T>();
        }

        template<typename T>
        const T *Get(flecs::entity e) const {
            return &e.get<T>();
        }

        template<typename T>
        bool Has(flecs::entity e) const {
            return e.has<T>();
        }

        template<typename T>
        void Remove(flecs::entity e) {
            e.remove<T>();
        }

        void RegisterComponents();

    private:
        flecs::world m_World;
    };

} // namespace Manro
