#pragma once

#include <Manro/ECS/Entity.h>
#include <vector>
#include <unordered_map>
#include <cassert>

namespace Manro {
    class IComponentArray {
    public:
        virtual ~IComponentArray() = default;

        virtual void EntityDestroyed(Entity entity) = 0;
    };

    template<typename T>
    class CComponentArray : public IComponentArray {
    public:
        CComponentArray() = default;

        void InsertData(Entity entity, T component) {
            assert(m_SparseMap.find(entity) == m_SparseMap.end() && "Component added to same entity more than once.");

            size_t newIndex = m_unSize;
            m_SparseMap[entity] = newIndex;

            if (newIndex >= m_ComponentArray.size()) {
                m_ComponentArray.push_back(std::move(component));
                m_DenseToEntityMap.push_back(entity);
            } else {
                m_ComponentArray[newIndex] = std::move(component);
                m_DenseToEntityMap[newIndex] = entity;
            }
            m_unSize++;
        }

        void RemoveData(Entity entity) {
            auto it = m_SparseMap.find(entity);
            assert(it != m_SparseMap.end() && "Removing non-existent component.");

            size_t indexOfRemovedElement = it->second;
            size_t indexOfLastElement = m_unSize - 1;

            m_ComponentArray[indexOfRemovedElement] = std::move(m_ComponentArray[indexOfLastElement]);

            Entity entityOfLastElement = m_DenseToEntityMap[indexOfLastElement];
            m_SparseMap[entityOfLastElement] = indexOfRemovedElement;
            m_DenseToEntityMap[indexOfRemovedElement] = entityOfLastElement;

            m_SparseMap.erase(entity);
            m_unSize--;
        }

        T &GetData(Entity entity) {
            auto it = m_SparseMap.find(entity);
            assert(it != m_SparseMap.end() && "Retrieving non-existent component.");
            return m_ComponentArray[it->second];
        }

        const T &GetData(Entity entity) const {
            auto it = m_SparseMap.find(entity);
            assert(it != m_SparseMap.end() && "Retrieving non-existent component.");
            return m_ComponentArray[it->second];
        }

        bool HasData(Entity entity) const {
            return m_SparseMap.find(entity) != m_SparseMap.end();
        }

        void EntityDestroyed(Entity entity) override {
            if (m_SparseMap.find(entity) != m_SparseMap.end()) {
                RemoveData(entity);
            }
        }

        std::vector<T> &GetDenseArray() { return m_ComponentArray; }

        const std::vector<T> &GetDenseArray() const { return m_ComponentArray; }

        size_t GetSize() const { return m_unSize; }

        const std::vector<Entity> &GetDenseToEntityMap() const { return m_DenseToEntityMap; }

    private:
        std::vector<T> m_ComponentArray;
        std::unordered_map<Entity, size_t> m_SparseMap;
        std::vector<Entity> m_DenseToEntityMap;
        size_t m_unSize{0};
    };
} // namespace Manro