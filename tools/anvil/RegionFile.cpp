#include "RegionFile.h"

#include <zlib.h>

#include <cstdio>
#include <unordered_map>
#include <utility>

namespace Manro::Anvil {
    bool InflateAuto(const u8 *src, size_t srcLen, std::vector<u8> &dst) {
        dst.clear();
        if (srcLen == 0 || srcLen > (1u << 30u))
            return false;
        z_stream strm{};
        strm.next_in = const_cast<Bytef *>(src);
        strm.avail_in = static_cast<uInt>(srcLen);
        if (inflateInit2(&strm, 15 + 32) != Z_OK)
            return false;
        std::vector<u8> chunk(1u << 16u);
        int ret = Z_OK;
        while (ret == Z_OK) {
            strm.next_out = chunk.data();
            strm.avail_out = static_cast<uInt>(chunk.size());
            ret = inflate(&strm, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                inflateEnd(&strm);
                return false;
            }
            dst.insert(dst.end(), chunk.data(),
                       chunk.data() + (chunk.size() - strm.avail_out));
            if (dst.size() > (64u << 20u)) {
                inflateEnd(&strm);
                return false;
            }
        }
        inflateEnd(&strm);
        return ret == Z_STREAM_END && !dst.empty();
    }

    bool ReadFile(const std::string &path, std::vector<u8> &out) {
        out.clear();
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f)
            return false;
        std::fseek(f, 0, SEEK_END);
        const long len = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (len <= 0 || len > (64L << 20L)) {
            std::fclose(f);
            return false;
        }
        out.resize(static_cast<size_t>(len));
        const size_t got = std::fread(out.data(), 1, out.size(), f);
        std::fclose(f);
        if (got != out.size()) {
            out.clear();
            return false;
        }
        return true;
    }

    namespace {
        int FloorDiv(int a, int b) {
            int q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0)))
                --q;
            return q;
        }

        u32 ReadU32BE(const u8 *p) {
            return (static_cast<u32>(p[0]) << 24u) | (static_cast<u32>(p[1]) << 16u) |
                   (static_cast<u32>(p[2]) << 8u) | p[3];
        }
    }

    struct CRegionFile::Impl {
        std::string regionDir;
        std::unordered_map<i64, std::vector<u8> > regionCache;

        static i64 RegionKey(int rx, int rz) {
            return (static_cast<i64>(rx) << 32) | (static_cast<u32>(rz));
        }

        bool LoadRegion(int rx, int rz, const std::vector<u8> *&bytes) {
            const i64 key = RegionKey(rx, rz);
            auto it = regionCache.find(key);
            if (it != regionCache.end()) {
                bytes = &it->second;
                return true;
            }
            static constexpr size_t kRegionCacheCap = 8;
            if (regionCache.size() >= kRegionCacheCap)
                regionCache.erase(regionCache.begin());
            char name[64];
            std::snprintf(name, sizeof(name), "/r.%d.%d.mca", rx, rz);
            std::vector<u8> data;
            if (!ReadFile(regionDir + name, data))
                return false;
            if (data.size() < 8192)
                return false;
            auto res = regionCache.emplace(key, std::move(data));
            bytes = &res.first->second;
            return true;
        }

        bool LoadChunkPayload(int cx, int cz, std::vector<u8> &payload) {
            payload.clear();
            const int rx = FloorDiv(cx, 32);
            const int rz = FloorDiv(cz, 32);
            const std::vector<u8> *bytes = nullptr;
            if (!LoadRegion(rx, rz, bytes))
                return false;
            const int lx = cx - rx * 32;
            const int lz = cz - rz * 32;
            const size_t idx = static_cast<size_t>(lx + lz * 32) * 4;
            if (idx + 4 > bytes->size())
                return false;
            const u32 loc = ReadU32BE(bytes->data() + idx);
            const u32 sector = loc >> 8u;
            const u32 sectorCount = loc & 0xFFu;
            if (sector < 2 || sectorCount == 0)
                return false;
            const u32 off = sector * 4096u;
            if (off + 5 > bytes->size())
                return false;
            const u32 len = ReadU32BE(bytes->data() + off);
            if (len < 1 || len > sectorCount * 4096u - 4u || off + 4 + len > bytes->size())
                return false;
            const u8 type = bytes->data()[off + 4];
            const u8 *src = bytes->data() + off + 5;
            const size_t srcLen = len - 1;
            if (type == 3) {
                payload.assign(src, src + srcLen);
                return true;
            }
            if (type == 1 || type == 2)
                return InflateAuto(src, srcLen, payload);
            return false;
        }
    };

    CRegionFile::CRegionFile() = default;

    CRegionFile::~CRegionFile() = default;

    bool CRegionFile::Open(const std::string &regionDir) {
        if (regionDir.empty())
            return false;
        m_Impl = std::make_unique<Impl>();
        m_Impl->regionDir = regionDir;
        return true;
    }

    bool CRegionFile::IsOpen() const { return m_Impl != nullptr; }

    bool CRegionFile::LoadChunkPayload(int cx, int cz, std::vector<u8> &payload) {
        return m_Impl && m_Impl->LoadChunkPayload(cx, cz, payload);
    }

    const std::string &CRegionFile::GetRegionDir() const {
        static const std::string kEmpty;
        return m_Impl ? m_Impl->regionDir : kEmpty;
    }
}
