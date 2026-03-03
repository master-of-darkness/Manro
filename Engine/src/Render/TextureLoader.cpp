#include "TextureLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <Core/Logger.h>

namespace Engine {
    bool TextureLoader::Load(const std::string &filepath, TextureData &out) {
        stbi_set_flip_vertically_on_load(true);

        int width = 0, height = 0, srcChannels = 0;
        stbi_uc *data = stbi_load(filepath.c_str(), &width, &height, &srcChannels, STBI_rgb_alpha);

        if (!data) {
            LOG_ERROR("[TextureLoader] Failed to load image: {} - {}", filepath, stbi_failure_reason());
            return false;
        }

        out.width = width;
        out.height = height;
        out.channels = 4; // RGBA
        size_t byteCount = static_cast<size_t>(width) * height * 4;
        out.pixels.assign(data, data + byteCount);

        stbi_image_free(data);

        LOG_INFO("[TextureLoader] Loaded '{}' - {}x{} ({} src channels)", filepath, width, height, srcChannels);
        return true;
    }
}

// namespace Engine
