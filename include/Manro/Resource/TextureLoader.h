#pragma once

#include <Manro/Core/Types.h>
#include <string>
#include <vector>

namespace Engine {
    struct TextureData {
        std::vector<u8> pixels;
        int width{0};
        int height{0};
        int channels{4};
    };

    class TextureLoader {
    public:
        static bool Load(const std::string &filepath, TextureData &out);
    };
} // namespace Engine
