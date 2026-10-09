#include "VoxelAnvilReader.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace Manro {
    namespace {

        struct NbtNode {
            u8 type{0};

            i64 i{0};
            double d{0.0};
            std::string s;
            std::vector<NbtNode> items;
            std::vector<std::string> keys;
            u8 listType{0};
        };

        struct Cursor {
            const u8 *p{nullptr};
            size_t n{0};
            size_t o{0};
            bool ok{true};

            bool Need(size_t k) {
                if (o + k > n) {
                    ok = false;
                    return false;
                }
                return true;
            }
            u8 U8() {
                if (!Need(1))
                    return 0;
                return p[o++];
            }
            u16 U16() {
                if (!Need(2))
                    return 0;
                const u16 v = static_cast<u16>((static_cast<u16>(p[o]) << 8u) | p[o + 1]);
                o += 2;
                return v;
            }
            i16 I16() { return static_cast<i16>(U16()); }
            i32 I32() {
                if (!Need(4))
                    return 0;
                i32 v = 0;
                for (int k = 0; k < 4; ++k)
                    v = (v << 8) | p[o + static_cast<size_t>(k)];
                o += 4;
                return v;
            }
            i64 I64() {
                if (!Need(8))
                    return 0;
                i64 v = 0;
                for (int k = 0; k < 8; ++k)
                    v = (v << 8) | p[o + static_cast<size_t>(k)];
                o += 8;
                return v;
            }
            float F32() {
                const i32 b = I32();
                float f = 0.f;
                std::memcpy(&f, &b, 4);
                return f;
            }
            double F64() {
                const i64 b = I64();
                double v = 0.0;
                std::memcpy(&v, &b, 8);
                return v;
            }
            std::string Str() {
                const u16 len = U16();
                if (!ok)
                    return {};
                if (!Need(len))
                    return {};
                std::string s(reinterpret_cast<const char *>(p + o), len);
                o += len;
                return s;
            }
        };

        bool ParsePayload(Cursor &c, u8 type, NbtNode &out);

        bool ParseCompoundBody(Cursor &c, NbtNode &out) {
            out.type = 10;
            for (;;) {
                if (!c.Need(1))
                    return false;
                const u8 t = c.U8();
                if (t == 0)
                    return c.ok;
                std::string name = c.Str();
                if (!c.ok)
                    return false;
                NbtNode v;
                if (!ParsePayload(c, t, v))
                    return false;
                out.keys.push_back(std::move(name));
                out.items.push_back(std::move(v));
            }
        }

        bool ParsePayload(Cursor &c, u8 type, NbtNode &out) {
            out.type = type;
            switch (type) {
            case 1:
                out.i = static_cast<i8>(c.U8());
                return c.ok;
            case 2:
                out.i = c.I16();
                return c.ok;
            case 3:
                out.i = c.I32();
                return c.ok;
            case 4:
                out.i = c.I64();
                return c.ok;
            case 5:
                out.d = c.F32();
                return c.ok;
            case 6:
                out.d = c.F64();
                return c.ok;
            case 7: {
                const i32 len = c.I32();
                if (!c.ok || len < 0 || static_cast<size_t>(len) > c.n)
                    return false;
                if (!c.Need(static_cast<size_t>(len)))
                    return false;
                out.s.assign(reinterpret_cast<const char *>(c.p + c.o),
                             static_cast<size_t>(len));
                c.o += static_cast<size_t>(len);
                return true;
            }
            case 8:
                out.s = c.Str();
                return c.ok;
            case 9: {
                if (!c.Need(1))
                    return false;
                out.listType = c.U8();
                const i32 len = c.I32();
                if (!c.ok || len < 0 || static_cast<size_t>(len) > c.n)
                    return false;
                out.items.reserve(static_cast<size_t>(len));
                for (i32 k = 0; k < len; ++k) {
                    NbtNode e;
                    if (!ParsePayload(c, out.listType, e))
                        return false;
                    out.items.push_back(std::move(e));
                }
                return true;
            }
            case 10:
                return ParseCompoundBody(c, out);
            case 11: {
                const i32 len = c.I32();
                if (!c.ok || len < 0 || static_cast<size_t>(len) > c.n)
                    return false;
                out.items.reserve(static_cast<size_t>(len));
                for (i32 k = 0; k < len; ++k) {
                    NbtNode e;
                    e.type = 3;
                    e.i = c.I32();
                    if (!c.ok)
                        return false;
                    out.items.push_back(std::move(e));
                }
                return true;
            }
            case 12: {
                const i32 len = c.I32();
                if (!c.ok || len < 0 || static_cast<size_t>(len) > c.n)
                    return false;
                out.items.reserve(static_cast<size_t>(len));
                for (i32 k = 0; k < len; ++k) {
                    NbtNode e;
                    e.type = 4;
                    e.i = c.I64();
                    if (!c.ok)
                        return false;
                    out.items.push_back(std::move(e));
                }
                return true;
            }
            default:
                return false;
            }
        }

        bool ParseNbt(const std::vector<u8> &buf, NbtNode &root) {
            Cursor c{buf.data(), buf.size(), 0, true};
            if (!c.Need(1))
                return false;
            const u8 t = c.U8();
            if (t != 10)
                return false;
            (void)c.Str();
            if (!c.ok)
                return false;
            return ParseCompoundBody(c, root);
        }

        const NbtNode *Find(const NbtNode &parent, const char *name) {
            if (parent.type != 10)
                return nullptr;
            for (size_t k = 0; k < parent.keys.size(); ++k) {
                if (parent.keys[k] == name)
                    return &parent.items[k];
            }
            return nullptr;
        }

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

        bool UnpackPalette(const std::vector<i64> &words, int bits, i32 out[4096]) {
            if (bits < 4)
                bits = 4;
            const size_t perLong = static_cast<size_t>(64 / bits);
            if (perLong == 0)
                return false;
            const u64 mask =
                (bits >= 64) ? ~0ull : ((1ull << static_cast<unsigned>(bits)) - 1ull);
            for (int i = 0; i < 4096; ++i) {
                const size_t wi = static_cast<size_t>(i) / perLong;
                if (wi >= words.size())
                    return false;
                const auto shift = static_cast<unsigned>((static_cast<size_t>(i) % perLong) *
                                                         static_cast<size_t>(bits));
                out[i] = static_cast<i32>((static_cast<u64>(words[wi]) >> shift) & mask);
            }
            return true;
        }

        constexpr int kMinSectionY = -4;
        constexpr int kMaxSectionY = 19;
        constexpr int kSectionCount = kMaxSectionY - kMinSectionY + 1;

        struct CachedChunk {
            bool present[kSectionCount]{false};
            std::vector<FullState> palettes[kSectionCount];
            std::vector<u16> indices[kSectionCount];
        };

        bool DecodePaletteEntry(const NbtNode &e, FullState &out) {
            if (e.type != 10)
                return false;
            const NbtNode *nm = Find(e, "Name");
            if (!nm || nm->type != 8)
                return false;
            out.name = nm->s;
            out.props.clear();
            if (const NbtNode *pr = Find(e, "Properties")) {
                if (pr->type == 10) {
                    for (size_t k = 0; k < pr->keys.size(); ++k) {
                        if (pr->items[k].type == 8)
                            out.props.emplace_back(pr->keys[k], pr->items[k].s);
                    }
                    std::sort(out.props.begin(), out.props.end(),
                              [](const auto &a, const auto &b) { return a.first < b.first; });
                }
            }
            return true;
        }
    }

    struct CAnvilWorldReader::Impl {
        std::string regionDir;
        std::string worldDir;
        std::unordered_map<i64, std::vector<u8>> regionCache;
        std::unordered_map<i64, CachedChunk> chunkCache;
        static constexpr size_t kChunkCacheCap = 64;

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

        const CachedChunk &DecodeColumn(int cx, int cz) {
            const i64 key = (static_cast<i64>(cx) << 32) | (static_cast<u32>(cz));
            auto it = chunkCache.find(key);
            if (it != chunkCache.end())
                return it->second;
            if (chunkCache.size() >= kChunkCacheCap)
                chunkCache.erase(chunkCache.begin());
            CachedChunk col{};
            std::vector<u8> payload;
            if (LoadChunkPayload(cx, cz, payload) && !payload.empty()) {
                NbtNode root;
                if (ParseNbt(payload, root)) {
                    if (const NbtNode *sections = Find(root, "sections")) {
                        if (sections->type == 9) {
                            for (const NbtNode &sec : sections->items) {
                                if (sec.type != 10)
                                    continue;
                                const NbtNode *yNode = Find(sec, "Y");
                                if (!yNode)
                                    continue;
                                const int sy = static_cast<int>(yNode->i);
                                const int si = sy - kMinSectionY;
                                if (si < 0 || si >= kSectionCount)
                                    continue;
                                const NbtNode *states = Find(sec, "block_states");
                                if (!states || states->type != 10)
                                    continue;
                                const NbtNode *palette = Find(*states, "palette");
                                if (!palette || palette->type != 9 || palette->items.empty())
                                    continue;
                                std::vector<FullState> pal;
                                pal.reserve(palette->items.size());
                                bool bad = false;
                                for (const NbtNode &e : palette->items) {
                                    FullState fs;
                                    if (!DecodePaletteEntry(e, fs)) {
                                        bad = true;
                                        break;
                                    }
                                    pal.push_back(std::move(fs));
                                }
                                if (bad)
                                    continue;
                                if (pal.size() == 1) {
                                    col.palettes[si] = pal;
                                    col.indices[si].assign(4096, 0);
                                    col.present[si] = true;
                                    continue;
                                }
                                const NbtNode *data = Find(*states, "data");
                                if (!data || data->type != 12)
                                    continue;
                                std::vector<i64> words;
                                words.reserve(data->items.size());
                                for (const NbtNode &w : data->items)
                                    words.push_back(w.i);
                                int bits = 4;
                                while ((1 << bits) < static_cast<int>(pal.size()))
                                    ++bits;
                                i32 idx[4096];
                                if (!UnpackPalette(words, bits, idx))
                                    continue;

                                for (int k = 0; k < 4096; ++k) {
                                    if (idx[k] < 0 ||
                                        static_cast<size_t>(idx[k]) >= pal.size())
                                        idx[k] = 0;
                                }
                                col.palettes[si] = pal;
                                col.indices[si].assign(4096, 0);
                                for (int k = 0; k < 4096; ++k)
                                    col.indices[si][static_cast<size_t>(k)] =
                                        static_cast<u16>(idx[k]);
                                col.present[si] = true;
                            }
                        }
                    }
                }
            }
            auto res = chunkCache.emplace(key, std::move(col));
            return res.first->second;
        }
    };

    CAnvilWorldReader::~CAnvilWorldReader() {
        delete m_Impl;
        m_Impl = nullptr;
    }

    bool CAnvilWorldReader::Open(const std::string &worldDir) {
        delete m_Impl;
        m_Impl = new Impl();
        m_WorldDir = worldDir;
        m_RegionDir.clear();
        m_bOpen = false;
        namespace fs = std::filesystem;
        static const char *kCandidates[] = {
            "/region",
            "/dimensions/minecraft/overworld/region",
        };
        std::error_code ec;
        for (const char *c : kCandidates) {
            const std::string dir = worldDir + c;
            if (!fs::is_directory(dir, ec))
                continue;
            for (auto it = fs::directory_iterator(dir, ec); it != fs::directory_iterator();
                 it.increment(ec)) {
                if (ec)
                    break;
                const std::string p = it->path().string();
                if (p.size() > 4 && p.compare(p.size() - 4, 4, ".mca") == 0) {
                    m_Impl->regionDir = dir;
                    m_Impl->worldDir = worldDir;
                    m_RegionDir = dir;
                    m_bOpen = true;
                    return true;
                }
            }
        }

        delete m_Impl;
        m_Impl = nullptr;
        return false;
    }

    bool CAnvilWorldReader::ReadSectionStates(int sx, int sy, int sz, AnvilSectionStates &out) {
        out.present = false;
        out.palette.clear();
        std::fill(std::begin(out.pal), std::end(out.pal), 0);
        if (!m_bOpen || !m_Impl)
            return false;
        const int si = sy - kMinSectionY;
        if (si < 0 || si >= kSectionCount)
            return false;
        const CachedChunk &col = m_Impl->DecodeColumn(sx, sz);
        if (!col.present[si])
            return false;
        out.palette = col.palettes[si];

        for (int ly = 0; ly < 16; ++ly) {
            for (int lz = 0; lz < 16; ++lz) {
                for (int lx = 0; lx < 16; ++lx)
                    out.pal[lx + ly * 16 + lz * 256] =
                        col.indices[si][static_cast<size_t>((ly << 8) | (lz << 4) | lx)];
            }
        }
        out.present = true;
        return true;
    }

    bool CAnvilWorldReader::ScanSaveStates(std::vector<FullState> &uniqueOut) {
        uniqueOut.clear();
        if (!m_bOpen || !m_Impl)
            return false;
        namespace fs = std::filesystem;
        std::error_code ec;
        std::vector<std::string> regions;
        for (auto it = fs::directory_iterator(m_Impl->regionDir, ec);
             it != fs::directory_iterator(); it.increment(ec)) {
            if (ec)
                break;
            const std::string p = it->path().string();
            if (p.size() > 4 && p.compare(p.size() - 4, 4, ".mca") == 0)
                regions.push_back(p);
        }
        std::sort(regions.begin(), regions.end());
        std::unordered_map<std::string, FullState> seen;
        for (const std::string &path : regions) {

            const std::string base = fs::path(path).filename().string();
            int rx = 0, rz = 0;
            if (std::sscanf(base.c_str(), "r.%d.%d.mca", &rx, &rz) != 2)
                continue;
            for (int lz = 0; lz < 32; ++lz) {
                for (int lx = 0; lx < 32; ++lx) {
                    const int cx = rx * 32 + lx;
                    const int cz = rz * 32 + lz;
                    const CachedChunk &col = m_Impl->DecodeColumn(cx, cz);
                    for (int si = 0; si < kSectionCount; ++si) {
                        if (!col.present[si])
                            continue;
                        for (const FullState &fs : col.palettes[si]) {
                            const std::string key = MakeStateKey(fs.name, fs.props);
                            if (seen.find(key) == seen.end())
                                seen.emplace(key, fs);
                        }
                    }
                }
            }
        }
        uniqueOut.reserve(seen.size());
        for (auto &kv : seen)
            uniqueOut.push_back(std::move(kv.second));
        std::sort(uniqueOut.begin(), uniqueOut.end(), [](const FullState &a, const FullState &b) {
            return MakeStateKey(a.name, a.props) < MakeStateKey(b.name, b.props);
        });
        return true;
    }

    bool CAnvilWorldReader::ReadSpawn(Vec3 &spawn) const {
        if (m_WorldDir.empty())
            return false;
        std::vector<u8> raw;
        if (!ReadFile(m_WorldDir + "/level.dat", raw) || raw.empty())
            return false;
        NbtNode root;
        if (!ParseNbt(raw, root)) {
            std::vector<u8> flat;
            if (!InflateAuto(raw.data(), raw.size(), flat))
                return false;
            if (!ParseNbt(flat, root))
                return false;
        }
        const NbtNode *data = Find(root, "Data");
        if (!data)
            data = &root;

        const NbtNode *x = Find(*data, "SpawnX");
        const NbtNode *y = Find(*data, "SpawnY");
        const NbtNode *z = Find(*data, "SpawnZ");
        if (x && y && z) {
            spawn =
                Vec3(static_cast<float>(x->i), static_cast<float>(y->i), static_cast<float>(z->i));
            return true;
        }

        if (const NbtNode *sp = Find(*data, "spawn")) {
            if (sp->type == 10) {
                if (const NbtNode *pos = Find(*sp, "pos")) {
                    if (pos->type == 11 && pos->items.size() == 3) {
                        spawn = Vec3(static_cast<float>(pos->items[0].i),
                                     static_cast<float>(pos->items[1].i),
                                     static_cast<float>(pos->items[2].i));
                        return true;
                    }
                }
            }
        }
        return false;
    }
}
