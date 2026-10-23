#include "BlockAssetInternal.h"

#include <sstream>

namespace Manro::blockassets {
    namespace {
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
                for (const auto &alt: when["OR"]) {
                    if (WhenMatches(alt, props))
                        return true;
                }
                return false;
            }
            if (!when.is_object())
                return false;
            for (const auto &[k, v]: when.items()) {
                if (k == "OR") {
                    bool any = false;
                    for (const auto &alt: v) {
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
                    for (const auto &opt: v) {
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

        bool IsNonOccluding(std::string_view name) {
            if (IsFluidName(name))
                return true;
            if (name == "ice" || name == "slime_block" || name == "honey_block" ||
                name == "beacon" || name == "conduit")
                return true;
            if (name.find("glass") != std::string_view::npos && name != "tinted_glass")
                return true;
            return name.find("leaves") != std::string_view::npos;
        }
    }

    std::vector<ModelRef> CollectModels(const json &blockJson,
                                        const std::map<std::string, std::string> &props) {
        if (blockJson.contains("variants") && blockJson["variants"].is_object()) {
            const auto &vars = blockJson["variants"];
            const json *bestEntry = nullptr;
            size_t bestCount = 0;
            bool bestExact = false;
            for (const auto &[key, val]: vars.items()) {
                const auto cond = ParsePropList(key);
                bool ok = true;
                for (const auto &[k, v]: cond) {
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
                    entry = &val[0];
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
                return {r};
            }
        }
        if (blockJson.contains("multipart") && blockJson["multipart"].is_array()) {
            std::vector<ModelRef> out;
            const json *fallback = nullptr;
            for (const auto &part: blockJson["multipart"]) {
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
                if (!part.contains("when") || WhenMatches(part["when"], props)) {
                    ModelRef r;
                    r.model = (*apply)["model"].get<std::string>();
                    r.x = apply->value("x", 0);
                    r.y = apply->value("y", 0);
                    out.push_back(std::move(r));
                }
            }
            if (!out.empty())
                return out;
            if (fallback) {
                ModelRef r;
                r.model = (*fallback)["model"].get<std::string>();
                r.x = fallback->value("x", 0);
                r.y = fallback->value("y", 0);
                return {r};
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
        return name == "end_portal" || name == "end_gateway" || name == "barrier" ||
               name == "light" || name == "structure_void" || name == "moving_piston" ||
               name == "bubble_column" || name == "nether_portal";
    }

    bool IsChainName(std::string_view name) {
        if (name == "chain_command_block")
            return false;
        if (name == "chain")
            return true;
        return name.size() > 6 &&
               name.compare(name.size() - 6, 6, "_chain") == 0;
    }

    bool IsFenceName(std::string_view name) {
        if (name.size() > 6 && name.compare(name.size() - 6, 6, "_fence") == 0)
            return true;
        if (name.size() > 11 &&
            name.compare(name.size() - 11, 11, "_fence_gate") == 0)
            return true;
        return false;
    }

    bool IsSignName(std::string_view name) {
        return name.find("sign") != std::string_view::npos;
    }

    bool IsSmallDecor(std::string_view name) {
        if (name == "snow")
            return true;
        if (name == "fire" || name == "soul_fire")
            return true;
        const auto has = [&](const char *sub) {
            return name.find(sub) != std::string_view::npos;
        };
        if (name == "flower_pot" || has("potted_"))
            return true;
        if (name == "kelp" || name == "kelp_plant")
            return true;
        if (name == "red_mushroom" || name == "brown_mushroom")
            return true;
        if (name == "nether_wart")
            return true;
        if (has("coral") && name.find("coral_block") == std::string_view::npos)
            return true;
        if (has("lantern") && name.find("sea_lantern") == std::string_view::npos &&
            name.find("jack_o_lantern") == std::string_view::npos)
            return true;
        if (has("flower") && has("leaves"))
            return false;
        static const char *kKeys[] = {
            "flower", "tulip", "sapling", "torch", "vine", "sprouts", "roots",
            "tall_grass", "short_grass", "fern", "bush", "dead_bush", "rail", "redstone_wire",
            "tripwire", "carpet", "lever", "pressure_plate", "ladder", "banner",
            "candle", "seagrass", "crop", "pitcher", "wheat",
            "carrots", "potatoes", "beetroots", "beetroot", "seeds", "melon_stem",
            "pumpkin_stem", "cocoa", "button", "hook", "lily_pad", "frogspawn",
            "dripleaf", "spore_blossom", "glow_lichen", "sculk_vein", "pointed_dripstone",
            "amethyst_bud", "_cluster", "chorus_", "sea_pickle", "campfire",
            "berries", "string", "eyeblossom", "leaf_litter", "hanging_moss", "dry_grass",
            "fungus", "propagule", "petals", "poppy", "dandelion", "allium", "bluet", "orchid",
            "cornflower", "daisy", "valley", "lilac", "peony", "rose", "sugar_cane", "potted_",
            "item_frame", "repeater", "comparator"
        };
        for (const char *k: kKeys) {
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

    bool IsLeavesName(std::string_view name) {
        return name.find("leaves") != std::string_view::npos;
    }

    bool IsTrapdoorName(std::string_view name) {
        return name.find("trapdoor") != std::string_view::npos;
    }

    bool IsInnerFaces(std::string_view name) {
        (void) name;
        return false;
    }

    bool IsFluidName(std::string_view name) {
        return name == "water" || name == "lava";
    }

    u8 EmissionForState(std::string_view bname,
                        const std::map<std::string, std::string> &props) {
        auto get = [&](const char *k) -> std::string_view {
            auto it = props.find(k);
            return it == props.end() ? std::string_view{} : std::string_view(it->second);
        };
        const auto lit = get("lit");
        const bool litFalse = lit == "false";
        const bool litTrue = lit == "true";

        if (bname == "lava" || bname == "lava_cauldron")
            return 15;
        if (bname == "beacon" || bname == "conduit" || bname == "copper_lantern")
            return 15;
        if (bname == "fire")
            return 15;
        if (bname == "glowstone" || bname == "jack_o_lantern" || bname == "lantern")
            return 15;
        if (bname == "sea_lantern" || bname == "shroomlight")
            return 15;
        if (bname == "ochre_froglight" || bname == "verdant_froglight" ||
            bname == "pearlescent_froglight")
            return 15;
        if (bname == "campfire")
            return litFalse ? 0 : 15;
        if (bname == "soul_campfire")
            return litFalse ? 0 : 10;
        if (bname == "redstone_lamp")
            return litTrue ? 15 : 0;
        if (bname == "copper_bulb")
            return litTrue ? 15 : 0;
        if (bname == "exposed_copper_bulb")
            return litTrue ? 12 : 0;
        if (bname == "weathered_copper_bulb")
            return litTrue ? 8 : 0;
        if (bname == "oxidized_copper_bulb")
            return litTrue ? 4 : 0;
        if (bname == "respawn_anchor") {
            const auto c = get("charges");
            if (c == "4")
                return 15;
            if (c == "3")
                return 11;
            if (c == "2")
                return 7;
            if (c == "1")
                return 3;
            return 0;
        }
        if (bname == "sea_pickle") {
            const auto p = get("pickles");
            if (p == "4")
                return 15;
            if (p == "3")
                return 12;
            if (p == "2")
                return 9;
            return 6;
        }
        if (bname == "cave_vines" || bname == "cave_vines_plant")
            return (get("berries") == "true") ? 14 : 0;
        if (bname == "torch" || bname == "wall_torch" || bname == "copper_torch" ||
            bname == "copper_wall_torch" || bname == "end_rod" || bname == "end_rod_wall")
            return 14;
        if (bname == "furnace" || bname == "blast_furnace" || bname == "smoker")
            return litTrue ? 13 : 0;
        if (bname == "nether_portal")
            return 11;
        if (bname == "crying_obsidian")
            return 10;
        if (bname == "soul_fire" || bname == "soul_lantern" || bname == "soul_torch" ||
            bname == "soul_wall_torch")
            return 10;
        if (bname == "redstone_ore" || bname == "deepslate_redstone_ore")
            return litTrue ? 9 : 0;
        if (bname.find("candle_cake") != std::string_view::npos)
            return litTrue ? 3 : 0;
        if (bname.find("candle") != std::string_view::npos) {
            if (!litTrue && props.count("lit"))
                return 0;
            const auto n = get("candles");
            if (n == "4")
                return 12;
            if (n == "3")
                return 9;
            if (n == "2")
                return 6;
            return 3;
        }
        if (bname == "enchanting_table" || bname == "ender_chest" ||
            bname == "glow_lichen")
            return 7;
        if (bname == "redstone_torch" || bname == "redstone_wall_torch")
            return litFalse ? 0 : 7;
        if (bname == "sculk_catalyst" || bname == "vault")
            return 6;
        if (bname == "amethyst_cluster")
            return 5;
        if (bname == "large_amethyst_bud")
            return 4;
        if (bname == "magma_block")
            return 3;
        if (bname == "medium_amethyst_bud")
            return 2;
        if (bname == "brewing_stand" || bname == "brown_mushroom" ||
            bname == "dragon_egg" || bname == "end_portal_frame" ||
            bname == "sculk_sensor" || bname == "calibrated_sculk_sensor" ||
            bname == "small_amethyst_bud")
            return 1;
        if (bname == "light") {
            const auto l = get("level");
            if (!l.empty()) {
                int v = 0;
                for (char ch: l) {
                    if (ch >= '0' && ch <= '9')
                        v = v * 10 + (ch - '0');
                }
                if (v < 0)
                    v = 0;
                if (v > 15)
                    v = 15;
                return static_cast<u8>(v);
            }
            return 15;
        }
        return 0;
    }

    u8 SkyOpacityForState(std::string_view bname, u32 flags) {
        if ((flags & kBlockFlagOpaque) != 0u)
            return 15;
        if ((flags & kBlockFlagSkip) != 0u)
            return 0;
        if (bname.find("glass") != std::string_view::npos && bname != "tinted_glass")
            return 0;
        if (bname.find("_pane") != std::string_view::npos || bname == "iron_bars")
            return 0;
        if (bname.find("bars") != std::string_view::npos)
            return 0;
        return 1;
    }

    bool IsOpaqueOccluder(std::string_view name) {
        if (IsNonOccluding(name))
            return false;
        if (IsCutout(name))
            return false;
        if (IsFluidName(name))
            return false;
        return true;
    }

    std::string OverrideTexture(std::string_view name) {
        if (name.find("_pane") != std::string_view::npos || name == "iron_bars")
            return "block/glass";

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
}
