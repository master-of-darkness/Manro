// VoxelMcAssets: vanilla blockstates/models -> per-state cube face tiles.
// See VoxelMcAssets.h. Pure file/JSON/PNG work; mcs tables are used
// header-only (block_states.inc) for state enumeration + occlusion.

#include "VoxelMcAssets.h"

// mcs committed tables (header-only, no mcs link): state enumeration strides,
// occlusion/opacity, block names. Include path added in engine CMake.
#include "mcs/protocol/block_states.hpp"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace Manro {
    namespace fs = std::filesystem;
    using json = nlohmann::json;
    namespace mcsPlay = mcs::protocol::play;

    namespace {
        // Our face order +X,-X,+Y,-Y,+Z,-Z <- MC model dirs.
        constexpr std::array<const char *, 6> kMcDirs = {"east", "west", "up", "down", "south",
                                                         "north"};

        // Model-space dir permutation under variant rotation. Vanilla applies
        // X then Y. y=90 sends model-north to world-east (furnace rule);
        // x=90 (right-hand about +X) sends up->south, south->down, down->north.
        void RotateDirs(std::array<std::string, 6> &tex, int rotX, int rotY) {
            auto rotY90 = [&]() {
                // east<-north, south<-east, west<-south, north<-west
                std::array<std::string, 6> t = tex;
                tex[0] = t[5];
                tex[4] = t[0];
                tex[1] = t[4];
                tex[5] = t[1];
            };
            auto rotX90 = [&]() {
                // up<-north, south<-up, down<-south, north<-down
                std::array<std::string, 6> t = tex;
                tex[2] = t[5];
                tex[4] = t[2];
                tex[3] = t[4];
                tex[5] = t[3];
            };
            rotX = ((rotX % 360) + 360) % 360;
            rotY = ((rotY % 360) + 360) % 360;
            for (int i = 0; i < rotX / 90; ++i)
                rotX90();
            for (int i = 0; i < rotY / 90; ++i)
                rotY90();
        }

        std::map<std::string, std::string> ParsePropList(const std::string &key) {
            std::map<std::string, std::string> out;
            std::stringstream ss(key);
            std::string item;
            while (std::getline(ss, item, ',')) {
                const auto eq = item.find('=');
                if (eq == std::string::npos)
                    continue;
                out[item.substr(0, eq)] = item.substr(eq + 1);
            }
            return out;
        }

        bool WhenMatches(const json &when, const std::map<std::string, std::string> &props) {
            if (when.is_null() || when.empty())
                return true;
            if (when.is_object() && when.contains("OR") && when.size() == 1) {
                for (const auto &alt : when["OR"]) {
                    if (WhenMatches(alt, props))
                        return true;
                }
                return false;
            }
            if (!when.is_object())
                return false;
            for (const auto &[k, v] : when.items()) {
                if (k == "OR") {
                    bool any = false;
                    for (const auto &alt : v) {
                        if (WhenMatches(alt, props)) {
                            any = true;
                            break;
                        }
                    }
                    if (!any)
                        return false;
                    continue;
                }
                const auto it = props.find(k);
                if (it == props.end())
                    return false;
                if (v.is_array()) {
                    bool any = false;
                    for (const auto &opt : v) {
                        if (opt.is_string() && opt.get<std::string>() == it->second) {
                            any = true;
                            break;
                        }
                    }
                    if (!any)
                        return false;
                } else if (v.is_string()) {
                    if (v.get<std::string>() != it->second)
                        return false;
                } else {
                    return false;
                }
            }
            return true;
        }

        struct ModelRef {
            std::string model;
            int x{0};
            int y{0};
        };

        // Pick the model for (blockJson, state props): variants exact/subset
        // match (most constraints wins), else multipart (unconditional case
        // first, else first when-match, else first case).
        ModelRef PickModel(const json &blockJson, const std::map<std::string, std::string> &props) {
            if (blockJson.contains("variants") && blockJson["variants"].is_object()) {
                const auto &vars = blockJson["variants"];
                const json *best = nullptr;
                const json *bestEntry = nullptr;
                size_t bestCount = 0;
                bool bestExact = false;
                for (const auto &[key, val] : vars.items()) {
                    const auto cond = ParsePropList(key);
                    bool ok = true;
                    for (const auto &[k, v] : cond) {
                        const auto it = props.find(k);
                        if (it == props.end() || it->second != v) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok)
                        continue;
                    const bool exact = cond.size() == props.size();
                    if (bestEntry && bestCount > cond.size())
                        continue;
                    if (bestEntry && bestCount == cond.size() && bestExact && !exact)
                        continue;
                    const json *entry = &val;
                    if (val.is_array()) {
                        if (val.empty())
                            continue;
                        entry = &val[0]; // deterministic: first rotation
                    }
                    if (!entry->is_object() || !entry->contains("model"))
                        continue;
                    best = &val;
                    bestEntry = entry;
                    bestCount = cond.size();
                    bestExact = exact;
                    (void)best;
                }
                if (bestEntry) {
                    ModelRef r;
                    r.model = (*bestEntry)["model"].get<std::string>();
                    r.x = bestEntry->value("x", 0);
                    r.y = bestEntry->value("y", 0);
                    return r;
                }
            }
            if (blockJson.contains("multipart") && blockJson["multipart"].is_array()) {
                const json *fallback = nullptr;
                for (const auto &part : blockJson["multipart"]) {
                    const json *apply = nullptr;
                    if (part.contains("apply")) {
                        apply = &part["apply"];
                        if (apply->is_array()) {
                            if (apply->empty())
                                continue;
                            apply = &(*apply)[0];
                        }
                    }
                    if (!apply || !apply->is_object() || !apply->contains("model"))
                        continue;
                    if (!fallback)
                        fallback = apply;
                    const bool noWhen = !part.contains("when");
                    if (noWhen) { // unconditional base piece wins outright
                        ModelRef r;
                        r.model = (*apply)["model"].get<std::string>();
                        r.x = apply->value("x", 0);
                        r.y = apply->value("y", 0);
                        return r;
                    }
                    if (WhenMatches(part["when"], props)) {
                        ModelRef r;
                        r.model = (*apply)["model"].get<std::string>();
                        r.x = apply->value("x", 0);
                        r.y = apply->value("y", 0);
                        return r;
                    }
                }
                if (fallback) {
                    ModelRef r;
                    r.model = (*fallback)["model"].get<std::string>();
                    r.x = fallback->value("x", 0);
                    r.y = fallback->value("y", 0);
                    return r;
                }
            }
            return {};
        }

        std::string StripPrefix(const std::string &s) {
            const auto c = s.find(':');
            return (c == std::string::npos) ? s : s.substr(c + 1);
        }

        // 26.2 texture values can be objects ({"sprite": "block/x", ...}):
        // unwrap to the sprite path, "" when unusable.
        std::string TextureString(const json &v) {
            if (v.is_string())
                return v.get<std::string>();
            if (v.is_object() && v.contains("sprite") && v["sprite"].is_string())
                return v["sprite"].get<std::string>();
            return {};
        }
        // Follow a #var chain to a concrete texture path (vars may reference
        // other vars, e.g. template parents map up->#end, end->block/x).
        std::string ResolveVar(const std::map<std::string, std::string> &vars,
                               const std::string &v) {
            std::string cur = v;
            for (int i = 0; i < 8 && !cur.empty() && cur[0] == '#'; ++i) {
                const auto it = vars.find(cur.substr(1));
                if (it == vars.end())
                    return {};
                cur = it->second;
            }
            return (!cur.empty() && cur[0] == '#') ? std::string() : cur;
        }

        struct FaceTextures {
            // texture var-or-path per our face order; empty = unresolved
            std::array<std::string, 6> tex{};
            bool any{false};
        };

        // Resolve one model file to per-face texture refs (still "#var" or
        // "block/name"). Merges parent chain; element faces win per dir in
        // order (base element first, so tint overlays don't replace bases);
        // missing dirs fall back to parent-template vars.
        FaceTextures ResolveModel(const std::string &modelsDir,
                                  std::unordered_map<std::string, json> &modelCache,
                                  const std::string &modelPath, int depth = 0) {
            FaceTextures out{};
            if (depth > 8 || modelPath.empty())
                return out;
            const std::string rel = StripPrefix(modelPath); // block/<name>
            auto it = modelCache.find(rel);
            if (it == modelCache.end()) {
                json j;
                std::ifstream f(modelsDir + "/" + rel + ".json");
                if (f) {
                    try {
                        f >> j;
                    } catch (...) {
                    }
                }
                it = modelCache.emplace(rel, std::move(j)).first;
            }
            const json &j = it->second;
            if (!j.is_object())
                return out;
            // Chain: self first, then ancestors. Vars merge root-first
            // (child overrides); element faces scan leaf-first (base piece
            // wins over tint overlays AND over parent templates).
            std::vector<const json *> chainJson;
            chainJson.push_back(&j);
            {
                std::string p = (j.contains("parent") && j["parent"].is_string())
                                    ? StripPrefix(j["parent"].get<std::string>())
                                    : std::string();
                for (int i = 0; i < 8 && !p.empty(); ++i) {
                    auto pit = modelCache.find(p);
                    if (pit == modelCache.end()) {
                        json pj;
                        std::ifstream pf(modelsDir + "/" + p + ".json");
                        if (pf) {
                            try {
                                pf >> pj;
                            } catch (...) {
                            }
                        }
                        pit = modelCache.emplace(p, std::move(pj)).first;
                    }
                    if (!pit->second.is_object())
                        break;
                    chainJson.push_back(&pit->second);
                    if (pit->second.contains("parent") && pit->second["parent"].is_string())
                        p = StripPrefix(pit->second["parent"].get<std::string>());
                    else
                        break;
                }
            }
            std::map<std::string, std::string> vars;
            for (auto cit = chainJson.rbegin(); cit != chainJson.rend(); ++cit) {
                const json &pj = **cit;
                if (pj.contains("textures") && pj["textures"].is_object()) {
                    for (const auto &[k, v] : pj["textures"].items()) {
                        const std::string s = TextureString(v);
                        if (!s.empty())
                            vars[k] = s;
                    }
                }
            }
            // Element faces, first definition per dir wins (leaf first).
            std::array<bool, 6> have{false, false, false, false, false, false};
            for (const json *ej : chainJson) {
                if (!ej->contains("elements") || !(*ej)["elements"].is_array())
                    continue;
                for (const auto &el : (*ej)["elements"]) {
                    if (!el.contains("faces") || !el["faces"].is_object())
                        continue;
                    for (size_t f = 0; f < 6; ++f) {
                        if (have[f])
                            continue;
                        const auto &faces = el["faces"];
                        if (faces.contains(kMcDirs[f]) && faces[kMcDirs[f]].is_object()) {
                            const auto &fd = faces[kMcDirs[f]];
                            if (fd.contains("texture")) {
                                const std::string t = TextureString(fd["texture"]);
                                if (!t.empty()) {
                                    out.tex[f] = t;
                                    have[f] = true;
                                    out.any = true;
                                }
                            }
                        }
                    }
                }
            }
            // Parent-template fallback for missing dirs (element-less
            // parents like cube_column carry only texture vars).
            const std::string leaf = (j.contains("parent") && j["parent"].is_string())
                                         ? StripPrefix(j["parent"].get<std::string>())
                                         : rel;
            auto varOr = [&](const std::string &v) -> std::string {
                if (!v.empty() && v[0] == '#')
                    return ResolveVar(vars, v);
                return v;
            };
            auto need = [&](size_t f, const std::string &v) {
                if (!have[f]) {
                    const std::string r = varOr(v);
                    if (!r.empty()) {
                        out.tex[f] = r;
                        have[f] = true;
                        out.any = true;
                    }
                }
            };
            if (leaf == "block/cube_all") {
                for (size_t f = 0; f < 6; ++f)
                    need(f, "#all");
            } else if (leaf == "block/cube_column") {
                need(2, "#end");
                need(3, "#end");
                for (size_t f : {0u, 1u, 4u, 5u})
                    need(f, "#side");
            } else if (leaf == "block/cube_top") {
                need(2, "#top");
                need(3, "#bottom");
                for (size_t f : {0u, 1u, 4u, 5u})
                    need(f, "#side");
            } else {
                // Generic: explicit dir vars, then side/all/end/top/bottom.
                static const std::array<const char *, 6> kDirVars = {"east", "west", "up",
                                                                     "down", "south", "north"};
                for (size_t f = 0; f < 6; ++f)
                    need(f, std::string("#") + kDirVars[f]);
                for (size_t f = 0; f < 6; ++f)
                    need(f, "#side");
                for (size_t f = 0; f < 6; ++f)
                    need(f, "#all");
            }
            // Resolve #vars left standing (chains included).
            for (size_t f = 0; f < 6; ++f) {
                if (!out.tex[f].empty() && out.tex[f][0] == '#') {
                    const std::string r = ResolveVar(vars, out.tex[f]);
                    if (std::getenv("MC_DEBUG_VARS") && r.empty())
                        std::printf("  resolve %s vars=%zu <miss>\n", out.tex[f].c_str(),
                                 vars.size());
                    out.tex[f] = r;
                }
            }
            return out;
        }

        // Plains-biome tint, baked (v1 has no biomes): these PNGs are
        // grayscale-ish and vanilla multiplies a biome tint at render.
        // Colors are vanilla plains values (grass #91BD59, oak #48B518...).
        u32 TintForPath(const std::string &path) {
            const auto base = path.rfind('/');
            const std::string name =
                (base == std::string::npos) ? path : path.substr(base + 1);
            if (name == "grass_block_top")
                return 0x91BD59u;
            if (name == "water_still" || name == "water_flow")
                return 0x3F76E4u;
            if (name == "oak_leaves" || name == "jungle_leaves" || name == "acacia_leaves" ||
                name == "dark_oak_leaves" || name == "azalea_leaves" ||
                name == "flowering_azalea_leaves")
                return 0x48B518u;
            if (name == "spruce_leaves")
                return 0x619961u;
            if (name == "birch_leaves")
                return 0x80A755u;
            if (name == "mangrove_leaves")
                return 0x92C648u;
            if (name == "cherry_leaves")
                return 0xB3D9ABu;
            if (name == "pale_oak_leaves")
                return 0x9DB384u;
            return 0xFFFFFFu;
        }

        bool IsFlora(std::string_view name) {
            // Small cross-quad/plate/utility blocks: render as air (v1).
            // Structural partials (slab/stair/fence/wall/door/pane) keep a
            // solid cube fallback so silhouettes stay closed.
            static const std::array<const char *, 44> kKeys = {
                "flower", "tulip", "mushroom", "sapling", "torch", "vine", "sprouts", "roots",
                "tall_grass", "short_grass", "fern", "bush", "dead_bush", "rail", "redstone_wire",
                "tripwire", "carpet", "button", "lever", "pressure_plate", "door", "trapdoor",
                "ladder", "sign", "banner", "candle", "snow", "coral", "seagrass", "kelp",
                "anvil", "grindstone", "bell", "lantern", "crop", "pitcher", "wheat", "carrots",
                "potatoes", "beetroots", "melon_stem", "pumpkin_stem", "cocoa", "nether_wart"};
            if (name == "snow")
                return true; // the layer; snow_block stays solid
            for (const char *k : kKeys) {
                if (name.find(k) != std::string_view::npos)
                    return true;
            }
            return false;
        }

        bool IsCutout(std::string_view name) {
            if (name.find("leaves") != std::string_view::npos)
                return true;
            if (name == "ice" || name == "cobweb" || name == "scaffolding" ||
                name == "azalea" || name == "mangrove_roots")
                return true;
            if (name.find("glass") != std::string_view::npos && name != "tinted_glass")
                return true;
            return false;
        }

        bool IsInnerFaces(std::string_view name) {
            // Same-state neighbor faces still emit (fancy leaves). Glass/ice
            // same-state faces cull (no vanilla inner faces).
            return name.find("leaves") != std::string_view::npos;
        }

        std::string OverrideTexture(std::string_view name) {
            // Block-entity / technical blocks with no cube model.
            if (name == "chest" || name == "trapped_chest")
                return "block/oak_planks";
            if (name == "ender_chest")
                return "block/obsidian";
            if (name == "bubble_column")
                return "block/water_still";
            if (name == "nether_portal")
                return "block/obsidian";
            if (name == "lava")
                return "block/lava_still";
            if (name == "water")
                return "block/water_still";
            if (name == "conduit")
                return "block/prismarine_bricks";
            if (name == "decorated_pot")
                return "block/terracotta";
            if (name == "shulker_box")
                return "block/purple_wool";
            // Shulker boxes / skulls / statues / copper chests are
            // block-entity rendered (no cube model): nearest solid tile.
            if (name.size() > 12 && name.compare(name.size() - 12, 12, "_shulker_box") == 0)
                return "block/" + std::string(name.substr(0, name.size() - 12)) + "_wool";
            if ((name.size() > 5 &&
                 (name.compare(name.size() - 5, 5, "skull") == 0 ||
                  name.compare(name.size() - 5, 5, "_head") == 0)) ||
                name == "dragon_head" || name == "piglin_head")
                return "block/bone_block_side";
            if (name.find("golem_statue") != std::string_view::npos) {
                if (name.find("oxidized") != std::string_view::npos)
                    return "block/oxidized_copper";
                if (name.find("weathered") != std::string_view::npos)
                    return "block/weathered_copper";
                if (name.find("exposed") != std::string_view::npos)
                    return "block/exposed_copper";
                return "block/copper_block";
            }
            if (name.find("copper_chest") != std::string_view::npos) {
                if (name.find("oxidized") != std::string_view::npos)
                    return "block/oxidized_copper";
                if (name.find("weathered") != std::string_view::npos)
                    return "block/weathered_copper";
                if (name.find("exposed") != std::string_view::npos)
                    return "block/exposed_copper";
                return "block/copper_block";
            }
            return {};
        }

        bool IsExplicitSkip(std::string_view name) {
            // Technical blocks with no usable cube texture.
            return name == "end_portal" || name == "end_gateway" || name == "barrier" ||
                   name == "light" || name == "structure_void" || name == "moving_piston";
        }

        bool IsAlwaysAir(std::string_view name) {
            return name == "air" || name == "cave_air" || name == "void_air";
        }

        // ---- Pack disk cache (startup latency) ----
        // Parsing ~1000 blockstate/model JSONs + decoding ~923 PNGs costs
        // ~330ms every launch. The result is cached in a versioned binary
        // blob next to the assets dir; a fingerprint over (path, size,
        // mtime) of the source trees invalidates it. Tile paths are
        // write-only downstream, so they are not stored.
        constexpr u32 kPackCacheVersion = 1;
        constexpr char kPackCacheMagic[8] = {'M', 'C', 'P', 'C', 'K', '0', '1', '\0'};

        u64 FingerprintTree(const std::string &root) {
            // FNV-1a over sorted (relpath, size, mtime). Directory walk is
            // milliseconds; parsing it replaces is hundreds of ms.
            std::vector<std::string> entries;
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (ec)
                    break;
                if (!it->is_regular_file(ec))
                    continue;
                entries.push_back(it->path().string());
            }
            std::sort(entries.begin(), entries.end());
            u64 h = 1469598103934665603ull;
            auto mix = [&](const void *data, size_t len) {
                const auto *p = static_cast<const u8 *>(data);
                for (size_t i = 0; i < len; ++i) {
                    h ^= static_cast<u64>(p[i]);
                    h *= 1099511628211ull;
                }
            };
            for (const auto &path : entries) {
                mix(path.data(), path.size());
                std::error_code ec2;
                const auto sz = fs::file_size(path, ec2);
                if (!ec2)
                    mix(&sz, sizeof(sz));
                const auto mt = fs::last_write_time(path, ec2);
                if (!ec2) {
                    const auto ticks = mt.time_since_epoch().count();
                    mix(&ticks, sizeof(ticks));
                }
            }
            return h;
        }

        std::string PackCachePath(const std::string &assetsDir) {
            return (fs::path(assetsDir) / ".." / "pack_cache_v1.bin").lexically_normal().string();
        }

        bool TryLoadPackCache(const std::string &assetsDir, const std::string &texDir,
                              const std::string &bsDir, const std::string &modelsDir,
                              size_t expectStates, McAssetPack_t &out) {
            const std::string path = PackCachePath(assetsDir);
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return false;
            char magic[8]{};
            u32 version = 0;
            u64 fingerprint = 0;
            f.read(magic, 8);
            f.read(reinterpret_cast<char *>(&version), sizeof(version));
            f.read(reinterpret_cast<char *>(&fingerprint), sizeof(fingerprint));
            if (!f || std::memcmp(magic, kPackCacheMagic, 8) != 0 || version != kPackCacheVersion)
                return false;
            const u64 want = FingerprintTree(texDir) ^ (FingerprintTree(bsDir) * 1099511628211ull) ^
                             (FingerprintTree(modelsDir) * 16777619ull);
            if (fingerprint != want)
                return false;
            u32 maxState = 0, mapped = 0, fallback = 0, skipped = 0, stateCount = 0, tileCount = 0;
            f.read(reinterpret_cast<char *>(&maxState), sizeof(maxState));
            f.read(reinterpret_cast<char *>(&mapped), sizeof(mapped));
            f.read(reinterpret_cast<char *>(&fallback), sizeof(fallback));
            f.read(reinterpret_cast<char *>(&skipped), sizeof(skipped));
            f.read(reinterpret_cast<char *>(&stateCount), sizeof(stateCount));
            if (!f || stateCount != expectStates || stateCount == 0 || stateCount > 1u << 20)
                return false;
            McAssetPack_t pack{};
            pack.states.resize(stateCount);
            for (size_t s = 0; s < stateCount; ++s) {
                u16 tiles[6]{};
                u32 flags = 0;
                f.read(reinterpret_cast<char *>(tiles), sizeof(tiles));
                f.read(reinterpret_cast<char *>(&flags), sizeof(flags));
                if (!f)
                    return false;
                for (int k = 0; k < 6; ++k)
                    pack.states[s].faces.tile[k] = tiles[k];
                pack.states[s].flags = flags;
            }
            f.read(reinterpret_cast<char *>(&tileCount), sizeof(tileCount));
            if (!f || tileCount == 0 || tileCount > 1u << 20)
                return false;
            pack.tiles.reserve(tileCount);
            pack.tilePaths.resize(tileCount);
            for (u32 t = 0; t < tileCount; ++t) {
                u32 byteSize = 0;
                f.read(reinterpret_cast<char *>(&byteSize), sizeof(byteSize));
                if (!f || byteSize == 0 || byteSize > 1u << 20)
                    return false;
                std::vector<u8> tile(byteSize);
                f.read(reinterpret_cast<char *>(tile.data()), static_cast<std::streamsize>(byteSize));
                if (!f)
                    return false;
                pack.tiles.push_back(std::move(tile));
            }
            pack.maxState = maxState;
            pack.mappedStates = mapped;
            pack.fallbackStates = fallback;
            pack.skippedStates = skipped;
            out = std::move(pack);
            return true;
        }

        void TrySavePackCache(const std::string &assetsDir, const std::string &texDir,
                              const std::string &bsDir, const std::string &modelsDir,
                              const McAssetPack_t &pack) {
            const u64 fingerprint = FingerprintTree(texDir) ^ (FingerprintTree(bsDir) * 1099511628211ull) ^
                                    (FingerprintTree(modelsDir) * 16777619ull);
            std::ofstream f(PackCachePath(assetsDir), std::ios::binary | std::ios::trunc);
            if (!f)
                return; // read-only asset dirs simply skip caching
            f.write(kPackCacheMagic, 8);
            f.write(reinterpret_cast<const char *>(&kPackCacheVersion), sizeof(kPackCacheVersion));
            f.write(reinterpret_cast<const char *>(&fingerprint), sizeof(fingerprint));
            const u32 stateCount = static_cast<u32>(pack.states.size());
            const u32 tileCount = static_cast<u32>(pack.tiles.size());
            f.write(reinterpret_cast<const char *>(&pack.maxState), sizeof(pack.maxState));
            f.write(reinterpret_cast<const char *>(&pack.mappedStates), sizeof(pack.mappedStates));
            f.write(reinterpret_cast<const char *>(&pack.fallbackStates), sizeof(pack.fallbackStates));
            f.write(reinterpret_cast<const char *>(&pack.skippedStates), sizeof(pack.skippedStates));
            f.write(reinterpret_cast<const char *>(&stateCount), sizeof(stateCount));
            for (const auto &s : pack.states) {
                f.write(reinterpret_cast<const char *>(s.faces.tile), sizeof(s.faces.tile));
                f.write(reinterpret_cast<const char *>(&s.flags), sizeof(s.flags));
            }
            f.write(reinterpret_cast<const char *>(&tileCount), sizeof(tileCount));
            for (const auto &tile : pack.tiles) {
                const u32 byteSize = static_cast<u32>(tile.size());
                f.write(reinterpret_cast<const char *>(&byteSize), sizeof(byteSize));
                f.write(reinterpret_cast<const char *>(tile.data()),
                        static_cast<std::streamsize>(tile.size()));
            }
        }
    } // namespace

    bool BuildMcAssetPack(const std::string &assetsDir, McAssetPack_t &out, std::string &err) {
        const std::string texDir = assetsDir + "/textures/block";
        const std::string bsDir = assetsDir + "/blockstates";
        const std::string modelsDir = assetsDir + "/models"; // rel paths include block/...
        std::error_code ec;
        if (!fs::is_directory(texDir, ec) || !fs::is_directory(bsDir, ec)) {
            err = "missing vanilla asset dirs under " + assetsDir;
            return false;
        }

        // Fast path: validated disk cache (JSON parse + PNG decode is ~330ms).
        if (TryLoadPackCache(assetsDir, texDir, bsDir, modelsDir,
                             mcsPlay::kBlockStateCollision.size(), out)) {
            std::printf("[McAssets] pack cache hit (%s)\n", PackCachePath(assetsDir).c_str());
            return true;
        }

        McAssetPack_t pack{};
        // Tile 0: missing-texture magenta.
        pack.tiles.emplace_back(16 * 16 * 4, 0);
        pack.tilePaths.emplace_back();
        for (int i = 0; i < 16 * 16; ++i) {
            pack.tiles[0][i * 4 + 0] = 255;
            pack.tiles[0][i * 4 + 1] = 0;
            pack.tiles[0][i * 4 + 2] = 255;
            pack.tiles[0][i * 4 + 3] = 255;
        }
        std::unordered_map<std::string, u16> tileByPath;
        auto tileForPath = [&](const std::string &path) -> u16 {
            if (path.empty())
                return 0;
            const auto it = tileByPath.find(path);
            if (it != tileByPath.end())
                return it->second;
            std::string rel = StripPrefix(path);
            if (rel.rfind("block/", 0) == 0)
                rel = rel.substr(6);
            const std::string file = texDir + "/" + rel + ".png";
            int w = 0, h = 0, comp = 0;
            stbi_uc *px = stbi_load(file.c_str(), &w, &h, &comp, 4);
            if (!px || w <= 0 || h <= 0) {
                if (std::getenv("MC_DEBUG_TEX"))
                    std::printf("  texfail path=%s file=%s\n", path.c_str(), file.c_str());
                if (px)
                    stbi_image_free(px);
                tileByPath[path] = 0;
                return 0;
            }
            // Animated strips: top width×width frame only.
            const int side = std::min(w, 16);
            std::vector<u8> tile(16 * 16 * 4, 0);
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    const int sx = (x * side) / 16;
                    const int sy = (y * side) / 16;
                    for (int c = 0; c < 4; ++c)
                        tile[(y * 16 + x) * 4 + c] = px[(sy * w + sx) * 4 + c];
                }
            }
            stbi_image_free(px);
            const u16 idx = static_cast<u16>(pack.tiles.size());
            // Bake the plains tint (see TintForPath) into the tile pixels.
            const u32 tint = TintForPath(path);
            if (tint != 0xFFFFFFu) {
                const float tr = static_cast<float>((tint >> 16u) & 0xFFu) / 255.f;
                const float tg = static_cast<float>((tint >> 8u) & 0xFFu) / 255.f;
                const float tb = static_cast<float>(tint & 0xFFu) / 255.f;
                for (int i = 0; i < 16 * 16; ++i) {
                    tile[i * 4 + 0] =
                        static_cast<u8>(static_cast<float>(tile[i * 4 + 0]) * tr + 0.5f);
                    tile[i * 4 + 1] =
                        static_cast<u8>(static_cast<float>(tile[i * 4 + 1]) * tg + 0.5f);
                    tile[i * 4 + 2] =
                        static_cast<u8>(static_cast<float>(tile[i * 4 + 2]) * tb + 0.5f);
                }
            }
            pack.tiles.push_back(std::move(tile));
            pack.tilePaths.push_back(path);
            tileByPath[path] = idx;
            return idx;
        };

        const size_t maxState = mcsPlay::kBlockStateCollision.size();
        pack.states.resize(maxState);
        pack.maxState = static_cast<u32>(maxState - 1);

        std::unordered_map<std::string, json> blockCache;
        std::unordered_map<std::string, json> modelCache;
        auto blockJson = [&](const std::string &name) -> const json * {
            const auto it = blockCache.find(name);
            if (it != blockCache.end())
                return &it->second;
            json j;
            std::ifstream f(bsDir + "/" + name + ".json");
            if (f) {
                try {
                    f >> j;
                } catch (...) {
                }
            }
            return &blockCache.emplace(name, std::move(j)).first->second;
        };

        u32 mapped = 0, fallback = 0, skipped = 0;
        // Per-block model-choice cache: variant selection depends only on
        // the constrained props, but evaluating per state is cheap enough
        // (~32k states) and keeps rotation handling exact.
        for (const auto &def : mcsPlay::kBlockStateDefs) {
            const std::string bname = StripPrefix(def.name);
            const std::string_view bview(bname);
            const bool flora = IsFlora(bview);
            const bool air = IsAlwaysAir(bview);
            // Enumerate the cartesian product of property values.
            std::vector<std::pair<std::string, std::vector<std::string> > > props;
            for (u32 i = 0; i < def.property_count; ++i) {
                const auto &pd = mcsPlay::kBlockStateProperties[def.first_property + i];
                std::vector<std::string> vals;
                for (u32 v = 0; v < pd.value_count; ++v)
                    vals.emplace_back(mcsPlay::kBlockStatePropertyValues[pd.first_value + v]);
                props.emplace_back(pd.name, std::move(vals));
            }
            std::vector<u32> idx(props.size(), 0);
            const auto *bj = blockJson(std::string(bname));
            const bool hasBj = bj && bj->is_object();
            const std::string texOverride = (!air && !flora) ? OverrideTexture(bname) : std::string();
            bool done = false;
            while (!done) {
                i32 state = def.base_state;
                std::map<std::string, std::string> pmap;
                for (size_t i = 0; i < props.size(); ++i) {
                    const auto &pd = mcsPlay::kBlockStateProperties[def.first_property +
                                                                   static_cast<u32>(i)];
                    state += static_cast<i32>(idx[i]) * pd.stride;
                    pmap[props[i].first] = props[i].second[idx[i]];
                }
                McStateLook_t look{};
                if (air || flora || IsExplicitSkip(bname)) {
                    look.flags = kMcFlagSkip;
                    ++skipped;
                } else if (!texOverride.empty()) {
                    const u16 t = tileForPath(texOverride);
                    for (size_t f = 0; f < 6; ++f)
                        look.faces.tile[f] = t;
                    const u8 occ = mcsPlay::kBlockStateLightOcclusion[static_cast<size_t>(state)];
                    if (occ == 1)
                        look.flags |= kMcFlagOpaque;
                    if (IsCutout(bname))
                        look.flags |= kMcFlagCutout;
                    if (IsInnerFaces(bname))
                        look.flags |= kMcFlagInner;
                    ++mapped;
                } else if (!hasBj) {
                    if (std::getenv("MC_DEBUG"))
                        std::printf("  noblockstate %s\n", bname.c_str());
                    ++fallback;
                } else {
                    const ModelRef ref = PickModel(*bj, pmap);
                    if (ref.model.empty()) {
                        if (std::getenv("MC_DEBUG"))
                            std::printf("  nomodel %s\n", bname.c_str());
                        ++fallback;
                    } else {
                        FaceTextures ft = ResolveModel(modelsDir, modelCache, ref.model);
                        if (std::getenv("MC_DEBUG_TEX")) {
                            for (size_t f = 0; f < 6; ++f) {
                                if (!ft.tex[f].empty() && ft.tex[f][0] == '#')
                                    std::printf("  unresolved %s -> %s f%zu=%s\n", bname.c_str(),
                                             ref.model.c_str(), f, ft.tex[f].c_str());
                            }
                        }
                        if (std::getenv("MC_DEBUG_TEX")) {
                            for (size_t f = 0; f < 6; ++f) {
                                if (!ft.tex[f].empty() && ft.tex[f][0] == '#')
                                    std::printf("  unresolved %s -> %s f%zu=%s\n", bname.c_str(),
                                             ref.model.c_str(), f, ft.tex[f].c_str());
                            }
                        }
                        if (!ft.any) {
                            if (std::getenv("MC_DEBUG"))
                                std::printf("  notex %s -> %s\n", bname.c_str(), ref.model.c_str());
                            ++fallback;
                        } else {
                            RotateDirs(ft.tex, ref.x, ref.y);
                            // cube_column_horizontal: ends face the axis.
                            if (ref.model.find("cube_column") != std::string::npos) {
                                const auto ait = pmap.find("axis");
                                if (ait != pmap.end() && ait->second != "y") {
                                    // swap end tiles onto the axis faces
                                    const size_t ax = (ait->second == "x") ? 0u : 4u;
                                    const std::string endT = ft.tex[2];
                                    const std::string sideT = ft.tex[ax];
                                    ft.tex[2] = ft.tex[3] = sideT;
                                    ft.tex[ax] = ft.tex[ax + 1] = endT;
                                }
                            }
                            for (size_t f = 0; f < 6; ++f)
                                look.faces.tile[f] = tileForPath(ft.tex[f]);
                            const u8 occ =
                                mcsPlay::kBlockStateLightOcclusion[static_cast<size_t>(state)];
                            if (occ == 1)
                                look.flags |= kMcFlagOpaque;
                            if (IsCutout(bname))
                                look.flags |= kMcFlagCutout;
                            if (IsInnerFaces(bname))
                                look.flags |= kMcFlagInner;
                            ++mapped;
                        }
                    }
                }
                if (static_cast<size_t>(state) < pack.states.size())
                    pack.states[static_cast<size_t>(state)] = look;
                // odometer
                done = true;
                for (size_t i = 0; i < idx.size(); ++i) {
                    if (++idx[i] < props[i].second.size()) {
                        done = false;
                        break;
                    }
                    idx[i] = 0;
                }
                if (idx.empty())
                    done = true;
            }
        }
        // Air-adjacent safety: state 0 is air in every table.
        pack.states[0].flags = kMcFlagSkip;
        pack.mappedStates = mapped;
        pack.fallbackStates = fallback;
        pack.skippedStates = skipped;
        TrySavePackCache(assetsDir, texDir, bsDir, modelsDir, pack);
        out = std::move(pack);
        std::printf("[McAssets] states=%zu mapped=%u fallback=%u skipped=%u tiles=%zu\n", maxState,
                 mapped, fallback, skipped, out.tiles.size());
        return true;
    }
} // namespace Manro
