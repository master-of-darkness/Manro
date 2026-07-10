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