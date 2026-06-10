#include <Manro/Core/World.h>

namespace Manro
{
    CWorld::CWorld() {
        RegisterComponents();
    }

    CWorld::~CWorld() = default;

    void CWorld::RegisterComponents() {
        m_World.component<Position>();
        m_World.component<Rotation>();
        m_World.component<Scale>();
        m_World.component<LocalToWorld>();
        m_World.component<PhysicsBody>();
        m_World.component<Collider>();
        m_World.component<RigidBody>();
        m_World.component<Light>();
        m_World.component<Name>();
        m_World.component<ModelRef>();
    }

    flecs::entity CWorld::CreateEntity(const char *name) {
        if (name && name[0])
            return m_World.entity(name);
        return m_World.entity();
    }

    void CWorld::DestroyEntity(flecs::entity e) {
        e.destruct();
    }

    bool CWorld::IsValid(flecs::entity e) const {
        return e.is_valid();
    }

} // namespace Manro
