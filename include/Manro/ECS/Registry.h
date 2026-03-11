#pragma once

#include <Manro/ECS/Entity.h>
#include <Manro/ECS/ComponentArray.h>
#include <queue>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <cassert>
#include <functional>

namespace Engine {
    class Registry {
    public:
        Registry() {
            for (Entity entity = 0; entity < MAX_ENTITIES; ++entity) {
                m_AvailableEntities.push(entity);
            }
        }

        Entity CreateEntity() {
            assert(m_LivingEntityCount < MAX_ENTITIES && "Too many entities in existence.");
            Entity id = m_AvailableEntities.front();
            m_AvailableEntities.pop();
            m_LivingEntityCount++;
            return id;
        }

        void DestroyEntity(Entity entity) {
            assert(entity < MAX_ENTITIES && "Entity out of range.");
            m_Signatures[entity].reset();
            m_AvailableEntities.push(entity);
            m_LivingEntityCount--;

            for (auto const &pair: m_ComponentArrays) {
                auto const &componentArray = pair.second;
                componentArray->EntityDestroyed(entity);
            }
        }

        Signature GetSignature(Entity entity) {
            assert(entity < MAX_ENTITIES && "Entity out of range.");
            return m_Signatures[entity];
        }

        template<typename T>
        void RegisterComponent() {
            std::string typeName = typeid(T).name();
            assert(
                m_ComponentTypes.find(typeName) == m_ComponentTypes.end() &&
                "Registering component type more than once.");

            m_ComponentTypes.insert({typeName, m_NextComponentType});
            m_ComponentArrays.insert({typeName, std::make_shared<ComponentArray<T> >()});
            m_NextComponentType++;
        }

        template<typename T>
        void AddComponent(Entity entity, T component) {
            GetComponentArray<T>()->InsertData(entity, std::move(component));
            auto signature = m_Signatures[entity];
            signature.set(GetComponentType<T>(), true);
            m_Signatures[entity] = signature;
        }

        template<typename T>
        void RemoveComponent(Entity entity) {
            GetComponentArray<T>()->RemoveData(entity);
            auto signature = m_Signatures[entity];
            signature.set(GetComponentType<T>(), false);
            m_Signatures[entity] = signature;
        }

        template<typename T>
        T &GetComponent(Entity entity) {
            return GetComponentArray<T>()->GetData(entity);
        }

        template<typename T>
        bool HasComponent(Entity entity) {
            return m_Signatures[entity].test(GetComponentType<T>());
        }

        template<typename T>
        std::shared_ptr<ComponentArray<T> > GetComponentArray() {
            std::string typeName = typeid(T).name();
            assert(m_ComponentTypes.find(typeName) != m_ComponentTypes.end() && "Component not registered before use.");
            return std::static_pointer_cast<ComponentArray<T> >(m_ComponentArrays[typeName]);
        }

        template<typename T>
        void ForEach(const std::function<void(Entity, T &)> &callback) {
            auto arr = GetComponentArray<T>();
            auto &dense = arr->GetDenseArray();
            auto &entityMap = arr->GetDenseToEntityMap();
            for (size_t i = 0; i < arr->GetSize(); ++i) {
                callback(entityMap[i], dense[i]);
            }
        }

    private:
        template<typename T>
        u32 GetComponentType() {
            std::string typeName = typeid(T).name();
            assert(m_ComponentTypes.find(typeName) != m_ComponentTypes.end() && "Component not registered before use.");
            return m_ComponentTypes[typeName];
        }

        std::queue<Entity> m_AvailableEntities;
        u32 m_LivingEntityCount{0};

        std::array<Signature, MAX_ENTITIES> m_Signatures;
        std::unordered_map<std::string, u32> m_ComponentTypes;
        u32 m_NextComponentType{0};

        std::unordered_map<std::string, std::shared_ptr<IComponentArray> > m_ComponentArrays;
    };
} // namespace Engine
