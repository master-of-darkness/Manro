#pragma once

#include <Manro/ECS/Entity.h>
#include <vector>
#include <array>
#include <cassert>

namespace Engine {
    class IComponentArray {
    public:
        virtual ~IComponentArray() = default;

        virtual void EntityDestroyed(Entity entity) = 0;
    };

    template<typename T>
    class ComponentArray : public IComponentArray {
    public:
        ComponentArray() {
            m_SparseMap.fill(NULL_ENTITY);
        }

        void InsertData(Entity entity, T component) {
            assert(m_SparseMap[entity] == NULL_ENTITY && "Component added to same entity more than once.");

            size_t newIndex = m_Size;
            m_SparseMap[entity] = newIndex;
            m_DenseToEntityMap[newIndex] = entity;

            if (newIndex >= m_ComponentArray.size()) {
                m_ComponentArray.push_back(std::move(component));
            } else {
                m_ComponentArray[newIndex] = std::move(component);
            }
            m_Size++;
        }

        void RemoveData(Entity entity) {
            assert(m_SparseMap[entity] != NULL_ENTITY && "Removing non-existent component.");

            size_t indexOfRemovedElement = m_SparseMap[entity];
            size_t indexOfLastElement = m_Size - 1;

            m_ComponentArray[indexOfRemovedElement] = std::move(m_ComponentArray[indexOfLastElement]);

            Entity entityOfLastElement = m_DenseToEntityMap[indexOfLastElement];
            m_SparseMap[entityOfLastElement] = indexOfRemovedElement;
            m_DenseToEntityMap[indexOfRemovedElement] = entityOfLastElement;

            m_SparseMap[entity] = NULL_ENTITY;
            m_Size--;
        }

        T &GetData(Entity entity) {
            assert(m_SparseMap[entity] != NULL_ENTITY && "Retrieving non-existent component.");
            return m_ComponentArray[m_SparseMap[entity]];
        }

        void EntityDestroyed(Entity entity) override {
            if (m_SparseMap[entity] != NULL_ENTITY) {
                RemoveData(entity);
            }
        }

        std::vector<T> &GetDenseArray() { return m_ComponentArray; }
        size_t GetSize() const { return m_Size; }
        const std::array<Entity, MAX_ENTITIES> &GetDenseToEntityMap() const { return m_DenseToEntityMap; }

    private:
        std::vector<T> m_ComponentArray;
        std::array<Entity, MAX_ENTITIES> m_SparseMap;
        std::array<Entity, MAX_ENTITIES> m_DenseToEntityMap;
        size_t m_Size{0};
    };
} // namespace Engine
