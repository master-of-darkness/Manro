#pragma once

#include <Manro/Core/Types.h>

#include <string>
#include <vector>

namespace Manro::Anvil {
    struct NbtNode {
        u8 type{0};

        i64 i{0};
        double d{0.0};
        std::string s;
        std::vector<NbtNode> items;
        std::vector<std::string> keys;
        u8 listType{0};
    };

    bool ParseNbt(const std::vector<u8> &buf, NbtNode &root);

    const NbtNode *Find(const NbtNode &parent, const char *name);
}
