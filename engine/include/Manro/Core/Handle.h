#pragma once

#include <Manro/Core/Types.h>
#include <format>
#include <functional>

namespace Manro {
    template<typename Tag, typename Storage = u32>
    struct Handle {
        static constexpr Storage kIndexBits = 24;
        static constexpr Storage kGenBits = 8;
        static constexpr Storage kIndexMask = (Storage(1) << kIndexBits) - 1;
        static constexpr Storage kGenMask = (Storage(1) << kGenBits) - 1;
        static constexpr Storage kInvalid = 0;

        Storage packed = kInvalid;

        // Construction
        [[nodiscard]] static Handle Make(Storage index, Storage gen) {
            Handle h;
            h.packed = ((gen & kGenMask) << kIndexBits) | (index & kIndexMask);
            return h;
        }

        Handle operator++(int) {
            Handle temp = *this;
            ++(*this);
            return temp;
        }

        Handle &operator++() {
            Storage index = packed & kIndexMask;
            Storage gen = (packed >> kIndexBits) & kGenMask;
            if (++index > kIndexMask) {
                index = 1; // skip the all-zero invalid handle
                gen = (gen + 1) & kGenMask;
            }
            packed = (gen << kIndexBits) | index;
            return *this;
        }

        operator Storage() const { return packed; }

        Storage Index() const { return packed & kIndexMask; }

        Storage Generation() const { return (packed >> kIndexBits) & kGenMask; }

        [[nodiscard]] bool IsValid() const { return packed != kInvalid; }

        bool operator==(const Handle &o) const { return packed == o.packed; }

        bool operator!=(const Handle &o) const { return packed != o.packed; }

        bool operator<(const Handle &o) const { return packed < o.packed; }
    };

    template<typename T, typename HandleT>
    class SlotMap {
    public:
        explicit SlotMap(u32 capacity = 1024) {
            m_Slots.reserve(capacity);
            m_FreeList.reserve(capacity);
            m_Slots.push_back({T{}, 0, false});
        }

        [[nodiscard]] HandleT Insert(T value) {
            if (!m_FreeList.empty()) {
                u32 idx = m_FreeList.back();
                m_FreeList.pop_back();
                auto &slot = m_Slots[idx];
                slot.value = std::move(value);
                slot.occupied = true;
                ++m_Count;
                return HandleT::Make(idx, slot.generation);
            }
            u32 idx = static_cast<u32>(m_Slots.size());
            m_Slots.push_back({std::move(value), 0, true});
            ++m_Count;
            return HandleT::Make(idx, 0);
        }

        [[nodiscard]] bool Remove(HandleT handle) {
            if (!IsValid(handle)) return false;
            auto &slot = m_Slots[handle.Index()];
            slot.occupied = false;
            slot.generation = (slot.generation + 1) & HandleT::kGenMask;
            m_FreeList.push_back(handle.Index());
            --m_Count;
            return true;
        }

        T *Get(HandleT handle) {
            if (!IsValid(handle)) return nullptr;
            return &m_Slots[handle.Index()].value;
        }

        const T *Get(HandleT handle) const {
            if (!IsValid(handle)) return nullptr;
            return &m_Slots[handle.Index()].value;
        }

        [[nodiscard]] bool IsValid(HandleT handle) const {
            if (!handle.IsValid()) return false;
            u32 idx = handle.Index();
            if (idx >= m_Slots.size()) return false;
            const auto &slot = m_Slots[idx];
            return slot.occupied && slot.generation == handle.Generation();
        }

        template<typename Fn>
        void ForEach(Fn &&fn) {
            for (auto &slot: m_Slots) {
                if (slot.occupied) fn(slot.value);
            }
        }

        u32 Size() const {
            return m_Count;
        }

    private:
        struct Slot {
            T value;
            u8 generation;
            bool occupied;
        };

        std::vector<Slot> m_Slots;
        std::vector<u32> m_FreeList;
        u32 m_Count = 0;
    };
} // namespace Manro

template<typename Tag, typename Storage>
struct std::formatter<Manro::Handle<Tag, Storage> > : std::formatter<Storage> {
    auto format(const Manro::Handle<Tag, Storage> &h, std::format_context &ctx) const {
        return std::formatter<Storage>::format(h.packed, ctx);
    }
};

namespace std {
    template<typename Tag, typename Storage>
    struct hash<Manro::Handle<Tag, Storage> > {
        size_t operator()(const Manro::Handle<Tag, Storage> &h) const noexcept {
            return std::hash<Storage>{}(h.packed);
        }
    };
}