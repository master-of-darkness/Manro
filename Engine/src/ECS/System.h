#pragma once

#include "Entity.h"
#include <set>

namespace Engine {
    class System {
    public:
        virtual ~System() = default;

        void AddEntity(Entity e) { m_Entities.insert(e); }
        void RemoveEntity(Entity e) { m_Entities.erase(e); }
        const std::set<Entity> &GetEntities() const { return m_Entities; }

    protected:
        std::set<Entity> m_Entities;
    };
} // namespace Engine