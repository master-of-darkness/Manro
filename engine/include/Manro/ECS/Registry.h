#pragma once

#include <Manro/ECS/Entity.h>
#include <Manro/ECS/ComponentArray.h>
#include <Manro/ECS/ComponentTypeId.h>
#include <queue>
#include <memory>
#include <cassert>

namespace Manro {
    class CRegistry {
    public:
        CRegistry() {
            for (Entity e = 0; e < MAX_ENTITIES; ++e)
                m_Available.push(e);
            m_Arrays.reserve(64);
        }

        [[nodiscard]] Entity CreateEntity() {
            assert(m_unLivingCount < MAX_ENTITIES && "Too many entities.");
            Entity id = m_Available.front();
            m_Available.pop();
            ++m_unLivingCount;
            if (id >= m_Signatures.size())
                m_Signatures.resize(id + 1);
            m_Signatures[id].reset();
            return id;
        }

        void DestroyEntity(Entity entity) {
            assert(entity < m_Signatures.size());
            m_Signatures[entity].reset();
            m_Available.push(entity);
            --m_unLivingCount;
            for (auto &arr: m_Arrays)
                if (arr) arr->EntityDestroyed(entity);
        }

        Signature GetSignature(Entity entity) const {
            assert(entity < m_Signatures.size());
            return m_Signatures[entity];
        }

        template<typename T>
        void RegisterComponent() {
            u32 id = ComponentTypeId<T>();
            assert(id < MAX_COMPONENTS && "Component id exceeds MAX_COMPONENTS.");
            if (id >= m_Arrays.size())
                m_Arrays.resize(id + 1);
            assert(!m_Arrays[id] && "Component registered twice.");
            m_Arrays[id] = std::make_unique<CComponentArray<T> >();
        }

        template<typename T>
        void AddComponent(Entity entity, T component) {
            GetArray<T>().InsertData(entity, std::move(component));
            if (entity >= m_Signatures.size())
                m_Signatures.resize(entity + 1);
            m_Signatures[entity].set(ComponentTypeId<T>(), true);
        }

        template<typename T>
        void RemoveComponent(Entity entity) {
            GetArray<T>().RemoveData(entity);
            m_Signatures[entity].set(ComponentTypeId<T>(), false);
        }

        template<typename T>
        T &GetComponent(Entity entity) {
            return GetArray<T>().GetData(entity);
        }

        template<typename T>
        const T &GetComponent(Entity entity) const {
            return GetArray<T>().GetData(entity);
        }

        template<typename T>
        [[nodiscard]] bool HasComponent(Entity entity) const {
            u32 id = ComponentTypeId<T>();
            if (id >= m_Arrays.size() || !m_Arrays[id]) return false;
            if (entity >= m_Signatures.size()) return false;
            return m_Signatures[entity].test(id);
        }

        template<typename T, typename Fn>
        void ForEach(Fn &&fn) {
            auto &arr = GetArray<T>();
            auto &dense = arr.GetDenseArray();
            const auto &entityMap = arr.GetDenseToEntityMap();
            for (size_t i = 0; i < arr.GetSize(); ++i)
                fn(entityMap[i], dense[i]);
        }

        template<typename T>
        const CComponentArray<T> *GetComponentArrayRO() const {
            u32 id = ComponentTypeId<T>();
            if (id >= m_Arrays.size() || !m_Arrays[id]) return nullptr;
            return static_cast<const CComponentArray<T> *>(m_Arrays[id].get());
        }

    private:
        template<typename T>
        CComponentArray<T> &GetArray() {
            u32 id = ComponentTypeId<T>();
            assert(id < m_Arrays.size() && m_Arrays[id] && "Component not registered.");
            return *static_cast<CComponentArray<T> *>(m_Arrays[id].get());
        }

        template<typename T>
        const CComponentArray<T> &GetArray() const {
            u32 id = ComponentTypeId<T>();
            assert(id < m_Arrays.size() && m_Arrays[id] && "Component not registered.");
            return *static_cast<const CComponentArray<T> *>(m_Arrays[id].get());
        }

        std::queue<Entity> m_Available;
        u32 m_unLivingCount{0};
        std::vector<Signature> m_Signatures;
        std::vector<std::unique_ptr<IComponentArray> > m_Arrays;
    };
} // namespace Manro