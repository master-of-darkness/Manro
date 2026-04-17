#pragma once

#include <Manro/Core/Types.h>
#include <string>
#include <vector>

namespace Manro {
    struct TextureData_t {
        std::vector<u8> pixels;
        int width{0};
        int height{0};
        int channels{4};
    };

    class CJobSystem;

    class CTextureLoader {
    public:
        static std::vector<TextureData_t> Load(const std::vector<std::string> &filepaths, CJobSystem &jobs);

        static TextureData_t LoadOne(const std::string &filepath);

        static std::vector<TextureData_t> LoadCubemap(const std::string &filepath);
    };
} // namespace Manro