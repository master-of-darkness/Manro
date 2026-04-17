#pragma once

#include <Manro/ECS/Entity.h>
#include <set>

namespace Manro {
    class CSystem {
    public:
        virtual ~CSystem() = default;

        void AddEntity(Entity e) { m_Entities.insert(e); }

        void RemoveEntity(Entity e) { m_Entities.erase(e); }

        const std::set<Entity> &GetEntities() const { return m_Entities; }

    protected:
        std::set<Entity> m_Entities;
    };
} // namespace Manro