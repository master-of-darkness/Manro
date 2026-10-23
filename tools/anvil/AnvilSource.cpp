#include "AnvilSource.h"

#include "Nbt.h"
#include "RegionFile.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Manro {
    namespace {
        constexpr int kMinSectionY = -4;
        constexpr int kMaxSectionY = 19;
        constexpr int kSectionCount = kMaxSectionY - kMinSectionY + 1;

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

        struct CachedChunk {
            bool present[kSectionCount]{false};
            std::vector<VoxelBlockState> palettes[kSectionCount];
            std::vector<u16> indices[kSectionCount];
        };

        bool DecodePaletteEntry(const Anvil::NbtNode &e, VoxelBlockState &out) {
            if (e.type != 10)
                return false;
            const Anvil::NbtNode *nm = Anvil::Find(e, "Name");
            if (!nm || nm->type != 8)
                return false;
            out.name = nm->s;
            out.props.clear();
            if (const Anvil::NbtNode *pr = Anvil::Find(e, "Properties")) {
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

    struct CAnvilContentSource::Impl {
        std::string regionDir;
        std::string worldDir;
        Anvil::CRegionFile region;
        std::unordered_map<i64, CachedChunk> chunkCache;
        static constexpr size_t kChunkCacheCap = 64;

        const CachedChunk &DecodeColumn(int cx, int cz) {
            const i64 key = (static_cast<i64>(cx) << 32) | (static_cast<u32>(cz));
            auto it = chunkCache.find(key);
            if (it != chunkCache.end())
                return it->second;
            if (chunkCache.size() >= kChunkCacheCap)
                chunkCache.erase(chunkCache.begin());
            CachedChunk col{};
            std::vector<u8> payload;
            if (region.LoadChunkPayload(cx, cz, payload) && !payload.empty()) {
                Anvil::NbtNode root;
                if (Anvil::ParseNbt(payload, root)) {
                    if (const Anvil::NbtNode *sections = Anvil::Find(root, "sections")) {
                        if (sections->type == 9) {
                            for (const Anvil::NbtNode &sec: sections->items) {
                                if (sec.type != 10)
                                    continue;
                                const Anvil::NbtNode *yNode = Anvil::Find(sec, "Y");
                                if (!yNode)
                                    continue;
                                const int sy = static_cast<int>(yNode->i);
                                const int si = sy - kMinSectionY;
                                if (si < 0 || si >= kSectionCount)
                                    continue;
                                const Anvil::NbtNode *states = Anvil::Find(sec, "block_states");
                                if (!states || states->type != 10)
                                    continue;
                                const Anvil::NbtNode *palette = Anvil::Find(*states, "palette");
                                if (!palette || palette->type != 9 || palette->items.empty())
                                    continue;
                                std::vector<VoxelBlockState> pal;
                                pal.reserve(palette->items.size());
                                bool bad = false;
                                for (const Anvil::NbtNode &e: palette->items) {
                                    VoxelBlockState fs;
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
                                const Anvil::NbtNode *data = Anvil::Find(*states, "data");
                                if (!data || data->type != 12)
                                    continue;
                                std::vector<i64> words;
                                words.reserve(data->items.size());
                                for (const Anvil::NbtNode &w: data->items)
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

    CAnvilContentSource::CAnvilContentSource() = default;

    CAnvilContentSource::~CAnvilContentSource() = default;

    bool CAnvilContentSource::Open(const std::string &worldDir) {
        m_Impl = std::make_unique<Impl>();
        m_WorldDir = worldDir;
        m_bOpen = false;
        namespace fs = std::filesystem;
        static const char *kCandidates[] = {
            "/region",
            "/dimensions/minecraft/overworld/region",
        };
        std::error_code ec;
        for (const char *c: kCandidates) {
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
                    m_Impl->region.Open(dir);
                    m_bOpen = true;
                    return true;
                }
            }
        }

        m_Impl.reset();
        return false;
    }

    bool CAnvilContentSource::IsOpen() const { return m_bOpen; }

    std::unique_ptr<IVoxelContentSource> CAnvilContentSource::Clone() const {
        return std::make_unique<CAnvilContentSource>();
    }

    bool CAnvilContentSource::ReadSectionStates(int sx, int sy, int sz,
                                                VoxelSectionStates &out) {
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

    bool CAnvilContentSource::ScanSaveStates(std::vector<VoxelBlockState> &uniqueOut) {
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
        std::unordered_map<std::string, VoxelBlockState> seen;
        for (const std::string &path: regions) {
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
                        for (const VoxelBlockState &fs: col.palettes[si]) {
                            const std::string key = MakeStateKey(fs.name, fs.props);
                            if (seen.find(key) == seen.end())
                                seen.emplace(key, fs);
                        }
                    }
                }
            }
        }
        uniqueOut.reserve(seen.size());
        for (auto &kv: seen)
            uniqueOut.push_back(std::move(kv.second));
        std::sort(uniqueOut.begin(), uniqueOut.end(),
                  [](const VoxelBlockState &a, const VoxelBlockState &b) {
                      return MakeStateKey(a.name, a.props) < MakeStateKey(b.name, b.props);
                  });
        return true;
    }

    bool CAnvilContentSource::ReadSpawn(Vec3 &spawn) const {
        if (m_WorldDir.empty())
            return false;
        std::vector<u8> raw;
        if (!Anvil::ReadFile(m_WorldDir + "/level.dat", raw) || raw.empty())
            return false;
        Anvil::NbtNode root;
        if (!Anvil::ParseNbt(raw, root)) {
            std::vector<u8> flat;
            if (!Anvil::InflateAuto(raw.data(), raw.size(), flat))
                return false;
            if (!Anvil::ParseNbt(flat, root))
                return false;
        }
        const Anvil::NbtNode *data = Anvil::Find(root, "Data");
        if (!data)
            data = &root;

        const Anvil::NbtNode *x = Anvil::Find(*data, "SpawnX");
        const Anvil::NbtNode *y = Anvil::Find(*data, "SpawnY");
        const Anvil::NbtNode *z = Anvil::Find(*data, "SpawnZ");
        if (x && y && z) {
            spawn =
                    Vec3(static_cast<float>(x->i), static_cast<float>(y->i), static_cast<float>(z->i));
            return true;
        }

        if (const Anvil::NbtNode *sp = Anvil::Find(*data, "spawn")) {
            if (sp->type == 10) {
                if (const Anvil::NbtNode *pos = Anvil::Find(*sp, "pos")) {
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
