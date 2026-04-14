#include <Manro/Resource/TextureLoader.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Core/JobSystem.h>
#include <../Core/Profiling.h>

#define STB_IMAGE_IMPLEMENTATION

#include <stb_image.h>

#include <Manro/Core/Logger.h>
#include <cstring>
#include <algorithm>

namespace Manro {
    static constexpr u32 kDDSMagic = 0x20534444;
    static constexpr u32 kFourCC_DXT1 = 0x31545844;
    static constexpr u32 kFourCC_DXT5 = 0x35545844;
    static constexpr u32 kFourCC_ATI2 = 0x32495441;

    struct DDSPixelFormat {
        u32 size;
        u32 flags;
        u32 fourCC;
        u32 rgbBitCount;
        u32 rBitMask, gBitMask, bBitMask, aBitMask;
    };

    struct DDSHeader {
        u32 size;
        u32 flags;
        u32 height;
        u32 width;
        u32 pitchOrLinearSize;
        u32 depth;
        u32 mipMapCount;
        u32 reserved1[11];
        DDSPixelFormat ddspf;
        u32 caps;
        u32 caps2;
        u32 caps3;
        u32 caps4;
        u32 reserved2;
    };

    static void DecodeDXT1Block(const u8 *block, u8 out[4][4][4]) {
        u16 c0 = block[0] | (block[1] << 8);
        u16 c1 = block[2] | (block[3] << 8);

        u8 colors[4][4];
        auto decode565 = [](u16 c, u8 *rgba) {
            rgba[0] = static_cast<u8>(((c >> 11) & 0x1F) * 255 / 31);
            rgba[1] = static_cast<u8>(((c >> 5) & 0x3F) * 255 / 63);
            rgba[2] = static_cast<u8>((c & 0x1F) * 255 / 31);
            rgba[3] = 255;
        };

        decode565(c0, colors[0]);
        decode565(c1, colors[1]);

        if (c0 > c1) {
            for (int i = 0; i < 3; ++i) {
                colors[2][i] = static_cast<u8>((2 * colors[0][i] + colors[1][i]) / 3);
                colors[3][i] = static_cast<u8>((colors[0][i] + 2 * colors[1][i]) / 3);
            }
            colors[2][3] = 255;
            colors[3][3] = 255;
        } else {
            for (int i = 0; i < 3; ++i)
                colors[2][i] = static_cast<u8>((colors[0][i] + colors[1][i]) / 2);
            colors[2][3] = 255;
            colors[3][0] = 0;
            colors[3][1] = 0;
            colors[3][2] = 0;
            colors[3][3] = 0;
        }

        u32 lookup = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) {
                int idx = (lookup >> (2 * (row * 4 + col))) & 0x3;
                std::memcpy(out[row][col], colors[idx], 4);
            }
    }

    static void DecodeDXT5Block(const u8 *block, u8 out[4][4][4]) {
        u8 a0 = block[0], a1 = block[1];
        u8 alphas[8];
        alphas[0] = a0;
        alphas[1] = a1;
        if (a0 > a1) {
            for (int i = 1; i <= 6; ++i)
                alphas[1 + i] = static_cast<u8>(((7 - i) * a0 + i * a1) / 7);
        } else {
            for (int i = 1; i <= 4; ++i)
                alphas[1 + i] = static_cast<u8>(((5 - i) * a0 + i * a1) / 5);
            alphas[6] = 0;
            alphas[7] = 255;
        }

        u64 abits = 0;
        for (int i = 0; i < 6; ++i)
            abits |= static_cast<u64>(block[2 + i]) << (8 * i);

        DecodeDXT1Block(block + 8, out);

        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) {
                int pixIdx = row * 4 + col;
                int aIdx = static_cast<int>((abits >> (3 * pixIdx)) & 0x7);
                out[row][col][3] = alphas[aIdx];
            }
    }

    static void DecodeATI2Block(const u8 *block, u8 out[4][4][4]) {
        auto decodeBC4 = [](const u8 *src, u8 channel[4][4]) {
            u8 v0 = src[0], v1 = src[1];
            u8 vals[8];
            vals[0] = v0;
            vals[1] = v1;
            if (v0 > v1) {
                for (int i = 1; i <= 6; ++i)
                    vals[1 + i] = static_cast<u8>(((7 - i) * v0 + i * v1) / 7);
            } else {
                for (int i = 1; i <= 4; ++i)
                    vals[1 + i] = static_cast<u8>(((5 - i) * v0 + i * v1) / 5);
                vals[6] = 0;
                vals[7] = 255;
            }

            u64 bits = 0;
            for (int i = 0; i < 6; ++i)
                bits |= static_cast<u64>(src[2 + i]) << (8 * i);

            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col) {
                    int pixIdx = row * 4 + col;
                    int idx = static_cast<int>((bits >> (3 * pixIdx)) & 0x7);
                    channel[row][col] = vals[idx];
                }
        };

        u8 redCh[4][4], greenCh[4][4];
        decodeBC4(block, redCh);
        decodeBC4(block + 8, greenCh);

        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) {
                out[row][col][0] = redCh[row][col];
                out[row][col][1] = greenCh[row][col];
                out[row][col][2] = 128;
                out[row][col][3] = 255;
            }
    }

    static bool LoadDDS(const std::vector<u8> &data, TextureData &out) {
        if (data.size() < 4 + sizeof(DDSHeader)) return false;

        u32 magic;
        std::memcpy(&magic, data.data(), 4);
        if (magic != kDDSMagic) return false;

        DDSHeader hdr{};
        std::memcpy(&hdr, data.data() + 4, sizeof(hdr));

        u32 fourCC = hdr.ddspf.fourCC;
        int blockSize;
        void(*decodeBlock)(const u8 *, u8[4][4][4]);

        if (fourCC == kFourCC_DXT1) {
            blockSize = 8;
            decodeBlock = DecodeDXT1Block;
        } else if (fourCC == kFourCC_DXT5) {
            blockSize = 16;
            decodeBlock = DecodeDXT5Block;
        } else if (fourCC == kFourCC_ATI2) {
            blockSize = 16;
            decodeBlock = DecodeATI2Block;
        } else {
            return false;
        }

        int w = static_cast<int>(hdr.width);
        int h = static_cast<int>(hdr.height);
        int bw = (w + 3) / 4;
        int bh = (h + 3) / 4;
        size_t dataSize = static_cast<size_t>(bw) * bh * blockSize;

        if (data.size() < 4 + sizeof(hdr) + dataSize) return false;
        const u8 *compressedData = data.data() + 4 + sizeof(hdr);

        out.width = w;
        out.height = h;
        out.channels = 4;
        out.pixels.resize(static_cast<size_t>(w) * h * 4);

        const u8 *src = compressedData;
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                u8 blockPixels[4][4][4];
                decodeBlock(src, blockPixels);
                src += blockSize;

                for (int row = 0; row < 4; ++row) {
                    int py = by * 4 + row;
                    if (py >= h) continue;
                    for (int col = 0; col < 4; ++col) {
                        int px = bx * 4 + col;
                        if (px >= w) continue;
                        size_t dstOff = (static_cast<size_t>(py) * w + px) * 4;
                        std::memcpy(&out.pixels[dstOff], blockPixels[row][col], 4);
                    }
                }
            }
        }

        return true;
    }

    static bool HasExtension(const std::string &path, const std::string &ext) {
        if (path.size() < ext.size()) return false;
        std::string tail = path.substr(path.size() - ext.size());
        std::ranges::transform(tail, tail.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return tail == ext;
    }

    static bool LoadIndividual(const std::string &filepath, TextureData &out) {
        auto &vfs = VirtualFS::Get();
        std::vector<u8> fileData = vfs.ReadFile(filepath);
        if (fileData.empty()) {
            return false;
        }

        if (HasExtension(filepath, ".dds")) {
            if (!LoadDDS(fileData, out)) {
                LOG_ERROR("[TextureLoader] Failed to load DDS: {}", filepath);
                return false;
            }
            return true;
        }

        stbi_set_flip_vertically_on_load(false);

        int width = 0, height = 0, srcChannels = 0;
        stbi_uc *data = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()),
                                              &width, &height, &srcChannels, STBI_rgb_alpha);

        if (!data) {
            LOG_ERROR("[TextureLoader] Failed to load image: {} - {}", filepath, stbi_failure_reason());
            return false;
        }

        out.width = width;
        out.height = height;
        out.channels = 4;
        size_t byteCount = static_cast<size_t>(width) * height * 4;
        out.pixels.assign(data, data + byteCount);

        stbi_image_free(data);

        return true;
    }

    std::vector<TextureData> TextureLoader::Load(const std::vector<std::string> &filepaths, JobSystem &jobs) {
        std::vector<TextureData> results(filepaths.size());
        const JobHandle handle = jobs.CreateHandle();

        for (size_t i = 0; i < filepaths.size(); ++i) {
            jobs.Execute(handle, [&filepaths, &results, i]() {
                LoadIndividual(filepaths[i], results[i]);
            });
        }
        jobs.Wait(handle);

        return results;
    }

    TextureData TextureLoader::LoadOne(const std::string &filepath) {
        MNR_PROFILE_SCOPE("LoadTexture");
        TextureData result;
        LoadIndividual(filepath, result);
        return result;
    }

    std::vector<TextureData> TextureLoader::LoadCubemap(const std::string &filepath) {
        TextureData rawData;
        if (!LoadIndividual(filepath, rawData)) {
            return {};
        }

        std::vector<TextureData> faces(6);
        int faceSize = 0;

        if (rawData.width == rawData.height * 4 / 3) {
            faceSize = rawData.height / 3;
        } else if (rawData.height == rawData.width * 4 / 3) {
            // TODO:
            // ... (3x4)
        } else if (rawData.width == rawData.height * 6) {
            // ... (6x1)
        } else if (rawData.height == rawData.width * 6) {
            // ... (1x6)
        } else {
            LOG_ERROR("[TextureLoader] Unsupported cubemap layout for {}: {}x{}", filepath, rawData.width,
                      rawData.height);
            return {};
        }

        for (int i = 0; i < 6; ++i) {
            const struct FaceCoord {
                int col, row;
            } coords[6] = {
                {2, 1}, // +X
                {0, 1}, // -X
                {1, 0}, // +Y
                {1, 2}, // -Y
                {1, 1}, // +Z
                {3, 1} // -Z
            };
            faces[i].width = faceSize;
            faces[i].height = faceSize;
            faces[i].channels = 4;
            faces[i].pixels.resize(static_cast<size_t>(faceSize) * faceSize * 4);

            int startX = coords[i].col * faceSize;
            int startY = coords[i].row * faceSize;

            for (int y = 0; y < faceSize; ++y) {
                const u8 *src = &rawData.pixels[((startY + y) * rawData.width + startX) * 4];
                u8 *dst = &faces[i].pixels[y * faceSize * 4];
                std::memcpy(dst, src, faceSize * 4);
            }
        }

        return faces;
    }
} // namespace Manro