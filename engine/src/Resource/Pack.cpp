#include <Manro/Resource/Pack.h>

#include <rres.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace Manro::Pack {
    namespace {
        constexpr std::uint16_t kRresVersion = 100;

        struct PackedEntry {
            std::string virtualPath;
            std::uint32_t id = 0;
            std::uint32_t chunkOffset = 0;
        };

        std::vector<std::uint8_t> ReadFileBytes(const fs::path &p) {
            std::ifstream in(p, std::ios::binary | std::ios::ate);
            if (!in) return {};
            const auto sz = static_cast<std::streamsize>(in.tellg());
            in.seekg(0);
            std::vector<std::uint8_t> bytes(static_cast<size_t>(sz));
            if (sz > 0) in.read(reinterpret_cast<char *>(bytes.data()), sz);
            return bytes;
        }

        std::string MakeVirtualPath(std::string_view prefix, const fs::path &rel) {
            std::string s = rel.generic_string();
            if (prefix.empty()) return s;
            std::string out(prefix);
            if (!out.empty() && out.back() == '/') out.pop_back();
            out.push_back('/');
            out.append(s);
            return out;
        }

        // RRES_DATA_RAW: propCount(u32) + 4 props(u32) + raw bytes
        std::vector<std::uint8_t> BuildRawPayload(const std::vector<std::uint8_t> &fileBytes,
                                                  const std::string &filename) {
            constexpr std::uint32_t propCount = 4;

            std::uint32_t ext1 = 0;
            std::uint32_t ext2 = 0;
            auto dot = filename.find_last_of('.');
            if (dot != std::string::npos) {
                std::string e = filename.substr(dot);
                for (size_t i = 0; i < e.size() && i < 4; ++i)
                    ext1 = (ext1 << 8) | static_cast<unsigned char>(e[i]);
                ext1 <<= (4 - std::min<size_t>(e.size(), 4)) * 8;
                if (e.size() > 4) {
                    for (size_t i = 4; i < e.size() && i < 8; ++i)
                        ext2 = (ext2 << 8) | static_cast<unsigned char>(e[i]);
                    ext2 <<= (8 - std::min<size_t>(e.size(), 8)) * 8;
                }
            }

            const std::uint32_t props[propCount] = {
                static_cast<std::uint32_t>(fileBytes.size()),
                ext1, ext2, 0u,
            };

            std::vector<std::uint8_t> payload;
            payload.resize(sizeof(std::uint32_t) * (1 + propCount) + fileBytes.size());
            std::uint8_t *p = payload.data();
            auto putU32 = [&](std::uint32_t v) {
                std::memcpy(p, &v, sizeof(v));
                p += sizeof(v);
            };
            putU32(propCount);
            for (std::uint32_t v: props) putU32(v);
            if (!fileBytes.empty())
                std::memcpy(p, fileBytes.data(), fileBytes.size());
            return payload;
        }

        std::vector<std::uint8_t> BuildCdirPayload(const std::vector<PackedEntry> &entries) {
            constexpr std::uint32_t propCount = 1;

            size_t total = sizeof(std::uint32_t) * (1 + propCount);
            std::vector<std::uint32_t> paddedSizes(entries.size());
            for (size_t i = 0; i < entries.size(); ++i) {
                const std::uint32_t rawLen =
                        static_cast<std::uint32_t>(entries[i].virtualPath.size()) + 1;
                const std::uint32_t padded = (rawLen + 3u) & ~3u;
                paddedSizes[i] = padded;
                total += 16 + padded;
            }

            std::vector<std::uint8_t> payload(total, 0);
            std::uint8_t *p = payload.data();
            auto putU32 = [&](std::uint32_t v) {
                std::memcpy(p, &v, sizeof(v));
                p += sizeof(v);
            };
            putU32(propCount);
            putU32(static_cast<std::uint32_t>(entries.size()));

            for (size_t i = 0; i < entries.size(); ++i) {
                putU32(entries[i].id);
                putU32(entries[i].chunkOffset);
                putU32(0);
                putU32(paddedSizes[i]);

                const auto &s = entries[i].virtualPath;
                std::memcpy(p, s.data(), s.size());
                p += paddedSizes[i];
            }
            return payload;
        }

        std::uint32_t WriteChunk(std::ofstream &os, const char fourCC[4],
                                 std::uint32_t resourceId,
                                 const std::vector<std::uint8_t> &payload) {
            const std::uint32_t offset = static_cast<std::uint32_t>(os.tellp());

            rresResourceChunkInfo info{};
            std::memcpy(info.type, fourCC, 4);
            info.id = resourceId;
            info.compType = RRES_COMP_NONE;
            info.cipherType = RRES_CIPHER_NONE;
            info.flags = 0;
            info.packedSize = static_cast<std::uint32_t>(payload.size());
            info.baseSize = static_cast<std::uint32_t>(payload.size());
            info.nextOffset = 0;
            info.reserved = 0;
            info.crc32 = rresComputeCRC32(payload.data(), static_cast<int>(payload.size()));

            os.write(reinterpret_cast<const char *>(&info), sizeof(info));
            os.write(reinterpret_cast<const char *>(payload.data()),
                     static_cast<std::streamsize>(payload.size()));
            return offset;
        }
    } // namespace

    PackResult PackDirectory(const fs::path &inputDir, const fs::path &outputRres,
                             const PackOptions &opts) {
        PackResult res{};

        const fs::path root = fs::absolute(inputDir);
        if (!fs::is_directory(root)) {
            res.error = "Input is not a directory: " + root.string();
            return res;
        }
        const fs::path outAbs = fs::absolute(outputRres);

        std::vector<fs::path> files;
        if (!opts.explicitFiles.empty()) {
            files.reserve(opts.explicitFiles.size());
            for (const auto &p: opts.explicitFiles) {
                std::error_code ec;
                if (fs::is_regular_file(p, ec)) files.push_back(fs::absolute(p));
            }
        } else {
            std::error_code itEc;
            for (auto it = fs::recursive_directory_iterator(root, itEc);
                 it != fs::recursive_directory_iterator(); ++it) {
                if (!it->is_regular_file()) continue;
                const fs::path &p = it->path();
                std::error_code ec;
                if (fs::exists(outAbs) && fs::equivalent(p, outAbs, ec)) continue;
                if (opts.skipHidden && p.filename().string().rfind('.', 0) == 0) continue;
                if (opts.skipEmpty && fs::file_size(p) == 0) continue;
                files.push_back(p);
            }
        }

        if (files.empty()) {
            res.error = "No files to pack under " + root.string();
            return res;
        }

        std::error_code mkEc;
        fs::create_directories(outAbs.parent_path(), mkEc);
        std::ofstream out(outAbs, std::ios::binary | std::ios::trunc);
        if (!out) {
            res.error = "Cannot open output: " + outAbs.string();
            return res;
        }

        rresFileHeader header{};
        header.id[0] = 'r';
        header.id[1] = 'r';
        header.id[2] = 'e';
        header.id[3] = 's';
        header.version = kRresVersion;
        header.chunkCount = 0;
        header.cdOffset = 0;
        header.reserved = 0;
        out.write(reinterpret_cast<const char *>(&header), sizeof(header));

        std::vector<PackedEntry> entries;
        entries.reserve(files.size());

        for (const auto &p: files) {
            fs::path rel = fs::relative(p, root);
            if (rel.empty() || rel.native().rfind("..", 0) == 0)
                rel = p.filename();

            std::string vpath = MakeVirtualPath(opts.prefix, rel);
            std::vector<std::uint8_t> bytes = ReadFileBytes(p);
            if (bytes.empty()) continue;

            std::uint32_t id = rresComputeCRC32(
                reinterpret_cast<const unsigned char *>(vpath.c_str()),
                static_cast<int>(vpath.size()));

            auto payload = BuildRawPayload(bytes, p.filename().string());
            std::uint32_t off = WriteChunk(out, "RAWD", id, payload);

            entries.push_back({vpath, id, off});
            if (opts.onFile) opts.onFile(vpath, bytes.size());
        }

        const std::uint32_t cdOffset = static_cast<std::uint32_t>(out.tellp());
        auto cdirPayload = BuildCdirPayload(entries);
        WriteChunk(out, "CDIR", 0, cdirPayload);

        header.chunkCount = static_cast<std::uint16_t>(entries.size() + 1);
        header.cdOffset = cdOffset - static_cast<std::uint32_t>(sizeof(rresFileHeader));
        out.seekp(0);
        out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        out.close();

        res.ok = true;
        res.entryCount = static_cast<std::uint32_t>(entries.size());
        res.cdOffset = header.cdOffset;
        return res;
    }
} // namespace Manro::Pack
