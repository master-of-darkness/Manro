#pragma once

#include <Manro/Core/Types.h>
#include <string>
#include <vector>

namespace Manro {
    struct TextureData {
        std::vector<u8> pixels;
        int width{0};
        int height{0};
        int channels{4};
    };

    class JobSystem;

    class TextureLoader {
    public:
        static std::vector<TextureData> Load(const std::vector<std::string> &filepaths, JobSystem &jobs);

        static std::vector<TextureData> LoadCubemap(const std::string &filepath);
    };
} // namespace Manro
