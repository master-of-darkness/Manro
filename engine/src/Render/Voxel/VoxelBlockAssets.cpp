// VoxelBlockAssets: save states -> per-state cube face tiles.
// Enumerates every distinct block state in the Anvil save, resolves each
// through the jar blockstates/models, and builds deduplicated 16x16 tiles.
// No third-party state tables; occlusion comes from model elements.

#include "VoxelBlockAssets.h"
#include "VoxelAnvilReader.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace Manro {
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    namespace {
        // Our face order +X,-X,+Y,-Y,+Z,-Z <- model dirs.
        constexpr std::array<const char *, 6> kModelDirs = {"east", "west", "up", "down",
                                                            "south", "north"};

        // Model-space dir permutation under variant rotation. Applies X then
        // Y. y=90 sends model-north to world-east (furnace rule); x=90
        // (right-hand about +X) sends up->south, south->down, down->north.
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
                    bestEntry = entry;
                    bestCount = cond.size();
                    bestExact = exact;
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

        std::string StripState(const std::string &s) {
            const auto b = s.find('[');
            return (b == std::string::npos) ? s : s.substr(0, b);
        }

        // Texture values can be objects ({"sprite": "block/x", ...}):
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

        const json *LoadModelJson(const std::string &modelsDir,
                                 std::unordered_map<std::string, json> &modelCache,
                                 const std::string &rel) {
            auto it = modelCache.find(rel);
            if (it != modelCache.end())
                return &it->second;
            json j;
            std::ifstream f(modelsDir + "/" + rel + ".json");
            if (f) {
                try {
                    f >> j;
                } catch (...) {
                }
            }
            return &modelCache.emplace(rel, std::move(j)).first->second;
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
                                  const std::string &modelPath) {
            FaceTextures out{};
            if (modelPath.empty())
                return out;
            const std::string rel = StripPrefix(modelPath); // block/<name>
            const json *root = LoadModelJson(modelsDir, modelCache, rel);
            // Chain: self first, then ancestors. Vars merge root-first
            // (child overrides); element faces scan leaf-first (base piece
            // wins over tint overlays AND over parent templates).
            std::vector<const json *> chainJson;
            if (root->is_object())
                chainJson.push_back(root);
            {
                std::string p =
                    (root->is_object() && root->contains("parent") && (*root)["parent"].is_string())
                        ? StripPrefix((*root)["parent"].get<std::string>())
                        : std::string();
                for (int i = 0; i < 8 && !p.empty(); ++i) {
                    const json *pj = LoadModelJson(modelsDir, modelCache, p);
                    if (!pj->is_object())
                        break;
                    chainJson.push_back(pj);
                    if (pj->contains("parent") && (*pj)["parent"].is_string())
                        p = StripPrefix((*pj)["parent"].get<std::string>());
                    else
                        break;
                }
            }
            if (chainJson.empty())
                return out;
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
                        if (faces.contains(kModelDirs[f]) && faces[kModelDirs[f]].is_object()) {
                            const auto &fd = faces[kModelDirs[f]];
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
            const json &j = *chainJson.front();
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
                if (!out.tex[f].empty() && out.tex[f][0] == '#')
                    out.tex[f] = ResolveVar(vars, out.tex[f]);
            }
            return out;
        }

        // True when the model (or any ancestor) has a full 16^3 element.
        // Rotation preserves fullness, so rotX/rotY need no handling here.
        // Kept for diagnostics; occlusion now follows baked-cube solidity
        // (see IsOpaqueOccluder), not source-model fullness.
        [[maybe_unused]] bool HasFullCubeElement(const std::string &modelsDir,
                               std::unordered_map<std::string, json> &modelCache,
                               const std::string &modelPath) {
            if (modelPath.empty())
                return false;
            std::string rel = StripPrefix(modelPath);
            for (int i = 0; i < 9 && !rel.empty(); ++i) {
                const json *j = LoadModelJson(modelsDir, modelCache, rel);
                if (j->is_object() && j->contains("elements") && (*j)["elements"].is_array()) {
                    for (const auto &el : (*j)["elements"]) {
                        if (!el.contains("from") || !el.contains("to"))
                            continue;
                        const auto &from = el["from"];
                        const auto &to = el["to"];
                        if (!from.is_array() || !to.is_array() || from.size() < 3 ||
                            to.size() < 3)
                            continue;
                        if (from[0].get<double>() <= 0.0 && from[1].get<double>() <= 0.0 &&
                            from[2].get<double>() <= 0.0 && to[0].get<double>() >= 16.0 &&
                            to[1].get<double>() >= 16.0 && to[2].get<double>() >= 16.0)
                            return true;
                    }
                }
                if (j->is_object() && j->contains("parent") && (*j)["parent"].is_string())
                    rel = StripPrefix((*j)["parent"].get<std::string>());
                else
                    break;
            }
            return false;
        }

        // Grayscale-ish textures the game tints at render (baked here).
        u32 TintForName(const std::string &name) {
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

        bool IsAlwaysAir(std::string_view name) {
            return name == "air" || name == "cave_air" || name == "void_air";
        }

        bool IsExplicitSkip(std::string_view name) {
            // Technical blocks with no usable cube texture.
            return name == "end_portal" || name == "end_gateway" || name == "barrier" ||
                   name == "light" || name == "structure_void" || name == "moving_piston" ||
                   name == "bubble_column" || name == "nether_portal";
        }

        bool IsSmallDecor(std::string_view name) {
            // Flat/cross-quad decor: render as air. Substring keys must not
            // swallow solid blocks (snow_block, *_coral_block, *_mushroom_block,
            // mushroom_stem, sea/jack-o-lanterns, flowering leaves, kelp blocks).
            if (name == "snow")
                return true; // the layer; snow_block stays solid
            if (name == "fire" || name == "soul_fire")
                return true; // cross-plane flames, not cubes
            const auto has = [&](const char *sub) {
                return name.find(sub) != std::string_view::npos;
            };
            if (name == "flower_pot" || has("potted_"))
                return true; // decorated_pot stays solid
            if (name == "kelp" || name == "kelp_plant")
                return true; // dried_kelp_block stays solid
            if (name == "red_mushroom" || name == "brown_mushroom")
                return true; // stems/blocks stay solid
            if (name == "chain")
                return true; // chain_command_block stays solid
            if (has("coral") && name.find("coral_block") == std::string_view::npos)
                return true;
            if (has("lantern") && name.find("sea_lantern") == std::string_view::npos &&
                name.find("jack_o_lantern") == std::string_view::npos)
                return true;
            if (has("flower") && has("leaves"))
                return false; // flowering azalea leaves stay solid-cutout
            static const char *kKeys[] = {
                "flower", "tulip", "sapling", "torch", "vine", "sprouts", "roots",
                "tall_grass", "short_grass", "fern", "bush", "dead_bush", "rail", "redstone_wire",
                "tripwire", "carpet", "lever", "pressure_plate", "ladder", "sign", "banner",
                "candle", "seagrass", "crop", "pitcher", "wheat",
                "carrots", "potatoes", "beetroots", "beetroot", "seeds", "melon_stem",
                "pumpkin_stem", "cocoa", "nether_wart", "button", "hook", "lily_pad", "frogspawn",
                "dripleaf", "spore_blossom", "glow_lichen", "sculk_vein", "pointed_dripstone",
                "amethyst_bud", "_cluster", "chorus_", "sea_pickle", "egg", "_rod", "campfire",
                "berries", "string", "eyeblossom", "leaf_litter", "hanging_moss", "dry_grass",
                "fungus", "propagule", "petals", "poppy", "dandelion", "allium", "bluet", "orchid",
                "cornflower", "daisy", "valley", "lilac", "peony", "rose", "sugar_cane", "potted_",
                "item_frame", "repeater", "comparator"};
            for (const char *k : kKeys) {
                if (has(k))
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
            if (name.find("bars") != std::string_view::npos)
                return true;
            return false;
        }

        bool IsInnerFaces(std::string_view name) {
            // Same-state interior faces are culled (fast leaves). Emitting
            // them (fancy) blows the 2048-face per-brick cache in dense
            // leaves/jungle: the task shader truncates the list and the
            // brick loses arbitrary faces = rectangular see-through holes.
            // Fast leaves look solid from outside with no holes.
            (void)name;
            return false;
        }

        bool IsFluidName(std::string_view name) {
            return name == "water" || name == "lava";
        }

        bool IsNonOccluding(std::string_view name) {
            // Full-cube models that still let neighbors render. Stained
            // glass variants count (only tinted glass truly occludes).
            if (IsFluidName(name))
                return true;
            if (name == "ice" || name == "slime_block" || name == "honey_block" ||
                name == "beacon" || name == "conduit")
                return true;
            if (name.find("glass") != std::string_view::npos && name != "tinted_glass")
                return true;
            return name.find("leaves") != std::string_view::npos;
        }

        bool IsOpaqueOccluder(std::string_view name) {
            // We bake every non-skipped block as a FULL cube (stairs/slabs/
            // doors/chests render as full quads), so occlusion must follow
            // the baked shape, not the source model: any solid-looking cube
            // occludes its neighbors. The old fullCube-only rule left
            // stairs/slabs/doors non-opaque, so every hidden interior face
            // between two solid-looking cubes emitted -> dense temple bricks
            // blew the 2048-face cache and lost arbitrary faces (holes).
            // Cutout/transparent cubes (glass/leaves/ice/scaffolding/...)
            // must stay non-opaque: they discard in the fragment shader, so
            // culling a neighbor behind them would punch a see-through hole.
            if (IsNonOccluding(name))
                return false;
            if (IsCutout(name))
                return false;
            if (IsFluidName(name))
                return false;
            return true;
        }

        std::string OverrideTexture(std::string_view name) {
            // Thin multipart blocks (panes/bars have per-arm models that never
            // cover all six faces): bake them as glass cubes with cutout.
            if (name.find("_pane") != std::string_view::npos || name == "iron_bars")
                return "block/glass";
            // Block-entity / technical blocks with no cube model.
            if (name == "chest" || name == "trapped_chest")
                return "block/oak_planks";
            if (name == "ender_chest")
                return "block/obsidian";
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

        // Load one texture, resampled to a 16x16 RGBA tile. Animated strips
        // (water/lava) use the top width x width frame only.
        bool LoadTile(const std::string &texDir, const std::string &path, std::vector<u8> &tile) {
            std::string rel = StripPrefix(path);
            if (rel.rfind("block/", 0) == 0)
                rel = rel.substr(6);
            const std::string file = texDir + "/" + rel + ".png";
            int w = 0, h = 0, comp = 0;
            stbi_uc *px = stbi_load(file.c_str(), &w, &h, &comp, 4);
            if (!px || w <= 0 || h <= 0) {
                if (px)
                    stbi_image_free(px);
                return false;
            }
            const int side = std::min(w, h);
            tile.assign(16 * 16 * 4, 0);
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    const int sx = (x * side) / 16;
                    const int sy = (y * side) / 16;
                    for (int c = 0; c < 4; ++c)
                        tile[(y * 16 + x) * 4 + c] = px[(sy * w + sx) * 4 + c];
                }
            }
            stbi_image_free(px);
            const auto slash = path.rfind('/');
            const std::string base =
                (slash == std::string::npos) ? path : path.substr(slash + 1);
            const u32 tint = TintForName(base);
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
            return true;
        }
    } // namespace

    bool BuildBlockAssetPack(const std::string &assetsDir, const std::string &worldDir,
                             BlockAssetPack_t &out, std::string &err) {
        const std::string texDir = assetsDir + "/textures/block";
        const std::string bsDir = assetsDir + "/blockstates";
        const std::string modelsDir = assetsDir + "/models"; // rel paths include block/...
        std::error_code ec;
        if (!fs::is_directory(texDir, ec) || !fs::is_directory(bsDir, ec)) {
            err = "missing block asset dirs under " + assetsDir;
            return false;
        }

        // Enumerate every distinct state in the save (plus always-includes).
        CAnvilWorldReader reader;
        if (!reader.Open(worldDir)) {
            err = "no region data in " + worldDir;
            return false;
        }
        std::vector<FullState> saveStates;
        if (!reader.ScanSaveStates(saveStates)) {
            err = "save scan failed for " + worldDir;
            return false;
        }
        std::unordered_map<std::string, FullState> byKey;
        for (FullState &fs : saveStates)
            byKey.emplace(MakeStateKey(fs.name, fs.props), std::move(fs));
        auto ensure = [&](const char *name) {
            FullState fs;
            fs.name = name;
            byKey.try_emplace(MakeStateKey(fs.name, fs.props), std::move(fs));
        };
        ensure("minecraft:air");
        ensure("minecraft:stone");
        ensure("minecraft:oak_planks");
        std::vector<FullState> ordered;
        ordered.reserve(byKey.size());
        for (auto &kv : byKey)
            ordered.push_back(std::move(kv.second));
        std::sort(ordered.begin(), ordered.end(), [](const FullState &a, const FullState &b) {
            if ((a.name == "minecraft:air") != (b.name == "minecraft:air"))
                return a.name == "minecraft:air";
            return MakeStateKey(a.name, a.props) < MakeStateKey(b.name, b.props);
        });
        if (ordered.size() > kBlockStateMax) {
            err = "too many distinct save states";
            return false;
        }

        BlockAssetPack_t pack{};
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
            std::vector<u8> tile;
            if (!LoadTile(texDir, path, tile)) {
                std::printf("[BlockAssets] missing texture %s\n", path.c_str());
                tileByPath[path] = 0;
                return 0;
            }
            const u16 idx = static_cast<u16>(pack.tiles.size());
            pack.tiles.push_back(std::move(tile));
            pack.tilePaths.push_back(path);
            tileByPath[path] = idx;
            return idx;
        };

        pack.states.resize(kBlockStateMax);
        for (auto &s : pack.states)
            s.flags = kBlockFlagSkip;
        pack.fluid.assign(kBlockStateMax, 0);

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
        u32 id = 0;
        for (const FullState &fs : ordered) {
            const std::string key = MakeStateKey(fs.name, fs.props);
            const std::string bname = StripState(StripPrefix(fs.name));
            const std::string_view bview(bname);
            std::map<std::string, std::string> pmap(fs.props.begin(), fs.props.end());

            BlockStateLook_t look{};
            bool isAir = IsAlwaysAir(bview) || IsExplicitSkip(bname) || IsSmallDecor(bview);
            if (isAir) {
                look.flags = kBlockFlagSkip;
                ++skipped;
            } else {
                const std::string texOverride = OverrideTexture(bname);
                const u32 fluid = IsFluidName(bview) ? 1u : 0u;
                if (!texOverride.empty()) {
                    const u16 t = tileForPath(texOverride);
                    if (t == 0)
                        ++fallback;
                    else
                        ++mapped;
                    for (size_t f = 0; f < 6; ++f)
                        look.faces.tile[f] = t;
                    // Baked stand-ins are full cubes: solid-looking ones
                    // (chests/skulls/pots) occlude like stone; cutout ones
                    // (panes/bars -> glass tile) must stay transparent or
                    // the neighbor cull + fragment discard punches holes.
                    look.flags = IsOpaqueOccluder(bname) ? kBlockFlagOpaque : 0u;
                    if (IsCutout(bname))
                        look.flags |= kBlockFlagCutout;
                    if (IsInnerFaces(bname))
                        look.flags |= kBlockFlagInner;
                } else {
                    const json *bj = blockJson(bname);
                    const ModelRef ref =
                        (bj && bj->is_object()) ? PickModel(*bj, pmap) : ModelRef{};
                    FaceTextures ft{};
                    if (!ref.model.empty())
                        ft = ResolveModel(modelsDir, modelCache, ref.model);
                    if (!ft.any) {
                        // Missing model -> magenta cube. It is solid and
                        // opaque (no alpha discard), so it must occlude;
                        // non-occluding here doubles hidden interior faces.
                        look.flags = kBlockFlagOpaque;
                        ++fallback; // magenta cube, occluding
                    } else {
                        RotateDirs(ft.tex, ref.x, ref.y);
                        // cube_column_horizontal: ends face the axis.
                        if (ref.model.find("cube_column") != std::string::npos) {
                            const auto ait = pmap.find("axis");
                            if (ait != pmap.end() && ait->second != "y") {
                                const size_t ax = (ait->second == "x") ? 0u : 4u;
                                const std::string endT = ft.tex[2];
                                const std::string sideT = ft.tex[ax];
                                ft.tex[2] = ft.tex[3] = sideT;
                                ft.tex[ax] = ft.tex[ax + 1] = endT;
                            }
                        }
                        for (size_t f = 0; f < 6; ++f)
                            look.faces.tile[f] = tileForPath(ft.tex[f]);
                        // Partial models (doors, bells): reuse a resolved
                        // sibling face instead of magenta for missing dirs.
                        u16 anyTile = 0;
                        for (size_t f = 0; f < 6; ++f) {
                            if (look.faces.tile[f] != 0u) {
                                anyTile = look.faces.tile[f];
                                break;
                            }
                        }
                        for (size_t f = 0; f < 6; ++f) {
                            if (look.faces.tile[f] == 0u)
                                look.faces.tile[f] = anyTile;
                        }
                        // Baked cubes occlude by tile solidity, not by source
                        // model fullness (see IsOpaqueOccluder): stairs/slabs
                        // are full quads here, so they must cull hidden
                        // interior faces or dense bricks overflow the
                        // 2048-face cache (holes).
                        look.flags = IsOpaqueOccluder(bname) ? kBlockFlagOpaque : 0u;
                        if (IsCutout(bname))
                            look.flags |= kBlockFlagCutout;
                        if (IsInnerFaces(bname))
                            look.flags |= kBlockFlagInner;
                        if (anyTile == 0u)
                            ++fallback; // all faces missing: magenta cube
                        else
                            ++mapped;
                    }
                }
                pack.fluid[id] = static_cast<u8>(fluid);
            }
            pack.states[id] = look;
            pack.stateByKey.emplace(key, id);
            ++id;
        }
        // Air-adjacent safety: state 0 is air in every table.
        pack.states[0].flags = kBlockFlagSkip;
        pack.fluid[0] = 0;
        pack.maxState = id - 1;
        pack.mappedStates = mapped;
        pack.fallbackStates = fallback;
        pack.skippedStates = skipped;
        const auto placeIt = pack.stateByKey.find(MakeStateKey("minecraft:oak_planks", {}));
        pack.placeState = (placeIt != pack.stateByKey.end()) ? placeIt->second : 1u;
        const auto stoneIt = pack.stateByKey.find(MakeStateKey("minecraft:stone", {}));
        pack.fallbackState = (stoneIt != pack.stateByKey.end()) ? stoneIt->second : 1u;
        out = std::move(pack);
        std::printf("[BlockAssets] states=%u mapped=%u fallback=%u skipped=%u tiles=%zu (%s)\n",
                    id, mapped, fallback, skipped, out.tiles.size(), assetsDir.c_str());
        return true;
    }
} // namespace Manro
