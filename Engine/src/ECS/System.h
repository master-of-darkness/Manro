#pragma once

#include "Entity.h"
#include <set>

namespace Engine {
    class System {
    public:
        virtual ~System() = default;

        std::set<Entity> m_Entities;
    };
} // namespace Engine