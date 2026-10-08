#include "VoxelBlockAssets.h"
#include "VoxelAnvilReader.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Manro {
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    namespace {
        constexpr std::array<const char *, 6> kModelDirs = {"east", "west", "up", "down",
                                                            "south", "north"};

        template <typename T> static void PermuteY90(std::array<T, 6> &a) {

            std::array<T, 6> t = a;
            a[0] = std::move(t[5]);
            a[4] = std::move(t[0]);
            a[1] = std::move(t[4]);
            a[5] = std::move(t[1]);
        }
        template <typename T> static void PermuteX90(std::array<T, 6> &a) {

            std::array<T, 6> t = a;
            a[2] = std::move(t[5]);
            a[4] = std::move(t[2]);
            a[3] = std::move(t[4]);
            a[5] = std::move(t[3]);
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

        std::vector<ModelRef> CollectModels(const json &blockJson,
                                            const std::map<std::string, std::string> &props) {
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

        std::string TextureString(const json &v) {
            if (v.is_string())
                return v.get<std::string>();
            if (v.is_object() && v.contains("sprite") && v["sprite"].is_string())
                return v["sprite"].get<std::string>();
            return {};
        }

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
            std::array<std::string, 6> tex{};
            std::array<std::array<float, 4>, 6> uv{};
            bool any{false};

            FaceTextures() {
                for (auto &r : uv)
                    r = {0.f, 0.f, 16.f, 16.f};
            }
        };

        static void RotateFaces(FaceTextures &ft, int rotX, int rotY) {
            rotX = ((rotX % 360) + 360) % 360;
            rotY = ((rotY % 360) + 360) % 360;
            for (int i = 0; i < rotX / 90; ++i) {
                PermuteX90(ft.tex);
                PermuteX90(ft.uv);
            }
            for (int i = 0; i < rotY / 90; ++i) {
                PermuteY90(ft.tex);
                PermuteY90(ft.uv);
            }
        }

        FaceTextures ResolveModel(const std::string &modelsDir,
                                  std::unordered_map<std::string, json> &modelCache,
                                  const std::string &modelPath) {
            FaceTextures out{};
            if (modelPath.empty())
                return out;
            const std::string rel = StripPrefix(modelPath);
            const json *root = LoadModelJson(modelsDir, modelCache, rel);
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

            std::array<bool, 6> have{false, false, false, false, false, false};
            for (const json *ej : chainJson) {
                if (!ej->contains("elements") || !(*ej)["elements"].is_array())
                    continue;
                std::array<std::string, 6> unionTex{};
                std::array<std::array<float, 4>, 6> unionMin{};
                std::array<std::array<float, 4>, 6> unionMax{};
                std::array<bool, 6> unionHave{false, false, false, false, false, false};
                std::array<bool, 6> unionFlipU{false, false, false, false, false, false};
                std::array<bool, 6> unionFlipV{false, false, false, false, false, false};
                for (auto &r : unionMin)
                    r = {16.f, 16.f, 16.f, 16.f};
                for (auto &r : unionMax)
                    r = {0.f, 0.f, 0.f, 0.f};
                for (const auto &el : (*ej)["elements"]) {
                    if (!el.contains("faces") || !el["faces"].is_object())
                        continue;
                    const auto &faces = el["faces"];
                    for (size_t f = 0; f < 6; ++f) {
                        if (!faces.contains(kModelDirs[f]) || !faces[kModelDirs[f]].is_object())
                            continue;
                        const auto &fd = faces[kModelDirs[f]];
                        if (!fd.contains("texture"))
                            continue;
                        const std::string t = TextureString(fd["texture"]);
                        if (t.empty())
                            continue;
                        std::array<float, 4> rect{0.f, 0.f, 16.f, 16.f};
                        if (fd.contains("uv") && fd["uv"].is_array() && fd["uv"].size() >= 4) {
                            for (int k = 0; k < 4; ++k)
                                rect[static_cast<size_t>(k)] =
                                    fd["uv"][static_cast<size_t>(k)].get<float>();
                        }
                        if (!unionHave[f]) {
                            unionTex[f] = t;
                            unionHave[f] = true;
                            unionMin[f][0] = std::min(rect[0], rect[2]);
                            unionMin[f][1] = std::min(rect[1], rect[3]);
                            unionMax[f][0] = std::max(rect[0], rect[2]);
                            unionMax[f][1] = std::max(rect[1], rect[3]);
                            unionFlipU[f] = rect[0] > rect[2];
                            unionFlipV[f] = rect[1] > rect[3];
                        } else if (unionTex[f] == t) {
                            unionMin[f][0] = std::min(unionMin[f][0], std::min(rect[0], rect[2]));
                            unionMin[f][1] = std::min(unionMin[f][1], std::min(rect[1], rect[3]));
                            unionMax[f][0] = std::max(unionMax[f][0], std::max(rect[0], rect[2]));
                            unionMax[f][1] = std::max(unionMax[f][1], std::max(rect[1], rect[3]));
                        }
                    }
                }
                for (size_t f = 0; f < 6; ++f) {
                    if (have[f] || !unionHave[f])
                        continue;
                    out.tex[f] = unionTex[f];
                    out.uv[f][0] = unionFlipU[f] ? unionMax[f][0] : unionMin[f][0];
                    out.uv[f][1] = unionFlipV[f] ? unionMax[f][1] : unionMin[f][1];
                    out.uv[f][2] = unionFlipU[f] ? unionMin[f][0] : unionMax[f][0];
                    out.uv[f][3] = unionFlipV[f] ? unionMin[f][1] : unionMax[f][1];
                    have[f] = true;
                    out.any = true;
                }
                bool allHave = true;
                for (bool h : have) {
                    if (!h) {
                        allHave = false;
                        break;
                    }
                }
                if (allHave)
                    break;
            }
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

                static const std::array<const char *, 6> kDirVars = {"east", "west", "up",
                                                                     "down", "south", "north"};
                for (size_t f = 0; f < 6; ++f)
                    need(f, std::string("#") + kDirVars[f]);
                for (size_t f = 0; f < 6; ++f)
                    need(f, "#side");
                for (size_t f = 0; f < 6; ++f)
                    need(f, "#all");
            }

            for (size_t f = 0; f < 6; ++f) {
                if (!out.tex[f].empty() && out.tex[f][0] == '#')
                    out.tex[f] = ResolveVar(vars, out.tex[f]);
            }
            return out;
        }

        struct Box3 {
            double mn[3]{0.0, 0.0, 0.0};
            double mx[3]{16.0, 16.0, 16.0};
        };

        static void RotX90Pt(double p[3]) {
            const double y = p[1], z = p[2];
            p[1] = 16.0 - z;
            p[2] = y;
        }

        static void RotY90Pt(double p[3]) {
            const double x = p[0], z = p[2];
            p[0] = 16.0 - z;
            p[2] = x;
        }

        static void RotateBox(Box3 &b, int rotX, int rotY) {
            rotX = ((rotX % 360) + 360) % 360;
            rotY = ((rotY % 360) + 360) % 360;
            if ((rotX % 90) != 0 || (rotY % 90) != 0)
                return;
            double pts[8][3];
            for (int i = 0; i < 8; ++i) {
                pts[i][0] = (i & 1) ? b.mx[0] : b.mn[0];
                pts[i][1] = (i & 2) ? b.mx[1] : b.mn[1];
                pts[i][2] = (i & 4) ? b.mx[2] : b.mn[2];
            }
            for (int k = 0; k < rotX / 90; ++k) {
                for (auto &p : pts)
                    RotX90Pt(p);
            }
            for (int k = 0; k < rotY / 90; ++k) {
                for (auto &p : pts)
                    RotY90Pt(p);
            }
            for (int a = 0; a < 3; ++a) {
                b.mn[a] = b.mx[a] = pts[0][a];
                for (int i = 1; i < 8; ++i) {
                    b.mn[a] = std::min(b.mn[a], pts[i][a]);
                    b.mx[a] = std::max(b.mx[a], pts[i][a]);
                }
            }
        }

        struct StateShape {
            double mn[3]{0.0, 0.0, 0.0};
            double mx[3]{16.0, 16.0, 16.0};
            bool hasElements{false};
        };

        StateShape CollectStateShape(const std::string &modelsDir,
                                     std::unordered_map<std::string, json> &modelCache,
                                     const std::vector<ModelRef> &refs) {
            StateShape out{};
            for (int pass = 0; pass < 2; ++pass) {
                bool first = true;
                for (const ModelRef &ref : refs) {
                    if (ref.model.empty())
                        continue;
                    std::string rel = StripPrefix(ref.model);
                    for (int depth = 0; depth < 9 && !rel.empty(); ++depth) {
                        const json *j = LoadModelJson(modelsDir, modelCache, rel);
                        bool found = false;
                        if (j->is_object() && j->contains("elements") &&
                            (*j)["elements"].is_array() && !(*j)["elements"].empty()) {
                            found = true;
                            for (const auto &el : (*j)["elements"]) {
                                if (!el.contains("from") || !el.contains("to"))
                                    continue;
                                const auto &from = el["from"];
                                const auto &to = el["to"];
                                if (!from.is_array() || !to.is_array() || from.size() < 3 ||
                                    to.size() < 3)
                                    continue;
                                Box3 b{{from[0].get<double>(), from[1].get<double>(),
                                        from[2].get<double>()},
                                       {to[0].get<double>(), to[1].get<double>(),
                                        to[2].get<double>()}};
                                bool planar = false;
                                for (int a = 0; a < 3; ++a) {
                                    if (std::abs(b.mx[a] - b.mn[a]) < 1e-6) {
                                        planar = true;
                                        break;
                                    }
                                }
                                if ((pass == 0) == planar)
                                    continue;
                                RotateBox(b, ref.x, ref.y);
                                if (first) {
                                    for (int a = 0; a < 3; ++a) {
                                        out.mn[a] = b.mn[a];
                                        out.mx[a] = b.mx[a];
                                    }
                                    first = false;
                                } else {
                                    for (int a = 0; a < 3; ++a) {
                                        out.mn[a] = std::min(out.mn[a], b.mn[a]);
                                        out.mx[a] = std::max(out.mx[a], b.mx[a]);
                                    }
                                }
                                out.hasElements = true;
                            }
                        }
                        if (found)
                            break;
                        if (j->is_object() && j->contains("parent") &&
                            (*j)["parent"].is_string())
                            rel = StripPrefix((*j)["parent"].get<std::string>());
                        else
                            break;
                    }
                }
                if (out.hasElements)
                    break;
            }
            return out;
        }

        std::vector<Box3> CollectSolidBoxes(const std::string &modelsDir,
                                            std::unordered_map<std::string, json> &modelCache,
                                            const std::vector<ModelRef> &refs) {
            std::vector<Box3> out;
            for (const ModelRef &ref : refs) {
                if (ref.model.empty())
                    continue;
                std::string rel = StripPrefix(ref.model);
                for (int depth = 0; depth < 9 && !rel.empty(); ++depth) {
                    const json *j = LoadModelJson(modelsDir, modelCache, rel);
                    bool found = false;
                    if (j->is_object() && j->contains("elements") &&
                        (*j)["elements"].is_array() && !(*j)["elements"].empty()) {
                        found = true;
                        for (const auto &el : (*j)["elements"]) {
                            if (!el.contains("from") || !el.contains("to"))
                                continue;
                            const auto &from = el["from"];
                            const auto &to = el["to"];
                            if (!from.is_array() || !to.is_array() || from.size() < 3 ||
                                to.size() < 3)
                                continue;
                            Box3 b{{from[0].get<double>(), from[1].get<double>(),
                                    from[2].get<double>()},
                                   {to[0].get<double>(), to[1].get<double>(),
                                    to[2].get<double>()}};
                            bool planar = false;
                            for (int a = 0; a < 3; ++a) {
                                if (std::abs(b.mx[a] - b.mn[a]) < 1e-6) {
                                    planar = true;
                                    break;
                                }
                            }
                            if (planar)
                                continue;
                            RotateBox(b, ref.x, ref.y);
                            out.push_back(b);
                        }
                    }
                    if (found)
                        break;
                    if (j->is_object() && j->contains("parent") &&
                        (*j)["parent"].is_string())
                        rel = StripPrefix((*j)["parent"].get<std::string>());
                    else
                        break;
                }
            }
            return out;
        }

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
            if (name == "chain")
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
                "tripwire", "carpet", "lever", "pressure_plate", "ladder", "sign", "banner",
                "candle", "seagrass", "crop", "pitcher", "wheat",
                "carrots", "potatoes", "beetroots", "beetroot", "seeds", "melon_stem",
                "pumpkin_stem", "cocoa", "button", "hook", "lily_pad", "frogspawn",
                "dripleaf", "spore_blossom", "glow_lichen", "sculk_vein", "pointed_dripstone",
                "amethyst_bud", "_cluster", "chorus_", "sea_pickle", "campfire",
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
            (void)name;
            return false;
        }

        bool IsFluidName(std::string_view name) {
            return name == "water" || name == "lava";
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

        bool LoadTile(const std::string &texDir, const std::string &path, std::vector<u8> &tile,
                      bool &outHasCutout) {
            outHasCutout = false;
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
            for (int i = 0; i < 16 * 16; ++i) {
                if (tile[i * 4 + 3] < 128u) {
                    outHasCutout = true;
                    break;
                }
            }
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
    }

    bool BuildBlockAssetPack(const std::string &assetsDir, const std::string &worldDir,
                             BlockAssetPack_t &out, std::string &err) {
        const std::string texDir = assetsDir + "/textures/block";
        const std::string bsDir = assetsDir + "/blockstates";
        const std::string modelsDir = assetsDir + "/models";
        std::error_code ec;
        if (!fs::is_directory(texDir, ec) || !fs::is_directory(bsDir, ec)) {
            err = "missing block asset dirs under " + assetsDir;
            return false;
        }

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

        pack.tiles.emplace_back(16 * 16 * 4, 0);
        pack.tilePaths.emplace_back();
        for (int i = 0; i < 16 * 16; ++i) {
            pack.tiles[0][i * 4 + 0] = 255;
            pack.tiles[0][i * 4 + 1] = 0;
            pack.tiles[0][i * 4 + 2] = 255;
            pack.tiles[0][i * 4 + 3] = 255;
        }
        std::unordered_map<std::string, u16> tileByPath;

        std::vector<u8> tileCutout{0};
        auto tileForPath = [&](const std::string &path) -> u16 {
            if (path.empty())
                return 0;
            const auto it = tileByPath.find(path);
            if (it != tileByPath.end())
                return it->second;
            std::vector<u8> tile;
            bool hasCutout = false;
            if (!LoadTile(texDir, path, tile, hasCutout)) {
                std::printf("[BlockAssets] missing texture %s\n", path.c_str());
                tileByPath[path] = 0;
                return 0;
            }
            const u16 idx = static_cast<u16>(pack.tiles.size());
            pack.tiles.push_back(std::move(tile));
            pack.tilePaths.push_back(path);
            tileCutout.push_back(hasCutout ? 1u : 0u);
            tileByPath[path] = idx;
            return idx;
        };

        auto facesNeedCutout = [&](const BlockFaceTiles_t &faces) -> bool {
            for (size_t f = 0; f < 6; ++f) {
                const u16 t = faces.tile[f];
                if (t < tileCutout.size() && tileCutout[t])
                    return true;
            }
            return false;
        };
        auto tileForOpaque = [&](const std::string &path, u16 origIdx) -> u16 {
            if (origIdx == 0 || origIdx >= pack.tiles.size())
                return origIdx;
            if (origIdx < tileCutout.size() && !tileCutout[origIdx])
                return origIdx;
            const std::string key = path + "#opaque";
            const auto it = tileByPath.find(key);
            if (it != tileByPath.end())
                return it->second;
            const std::vector<u8> &src = pack.tiles[origIdx];
            if (src.size() < 16 * 16 * 4)
                return origIdx;

            double acc[3]{0.0, 0.0, 0.0};
            int opaqueCount = 0;
            for (int i = 0; i < 16 * 16; ++i) {
                if (src[i * 4 + 3] >= 128u) {
                    acc[0] += src[i * 4 + 0];
                    acc[1] += src[i * 4 + 1];
                    acc[2] += src[i * 4 + 2];
                    ++opaqueCount;
                }
            }
            u8 fill[3]{139, 139, 139};
            if (opaqueCount > 0) {
                for (int c = 0; c < 3; ++c)
                    fill[c] = static_cast<u8>(acc[c] / opaqueCount + 0.5);
            }
            std::vector<u8> dst = src;
            for (int i = 0; i < 16 * 16; ++i) {
                if (dst[i * 4 + 3] < 128u) {
                    dst[i * 4 + 0] = fill[0];
                    dst[i * 4 + 1] = fill[1];
                    dst[i * 4 + 2] = fill[2];
                    dst[i * 4 + 3] = 255;
                } else {
                    dst[i * 4 + 3] = 255;
                }
            }
            const u16 idx = static_cast<u16>(pack.tiles.size());
            pack.tiles.push_back(std::move(dst));
            pack.tilePaths.push_back(key);
            tileCutout.push_back(0u);
            tileByPath[key] = idx;
            return idx;
        };

        auto rectIsOpaque = [&](const std::vector<u8> &tile, const std::array<float, 4> &uv) -> bool {
            if (tile.size() < 16 * 16 * 4)
                return false;
            const float u0 = std::clamp(std::min(uv[0], uv[2]), 0.f, 16.f);
            const float u1 = std::clamp(std::max(uv[0], uv[2]), 0.f, 16.f);
            const float v0 = std::clamp(std::min(uv[1], uv[3]), 0.f, 16.f);
            const float v1 = std::clamp(std::max(uv[1], uv[3]), 0.f, 16.f);
            int x0 = std::clamp(static_cast<int>(std::floor(u0)), 0, 16);
            int x1 = std::clamp(static_cast<int>(std::ceil(u1)), 0, 16);
            int y0 = std::clamp(static_cast<int>(std::floor(v0)), 0, 16);
            int y1 = std::clamp(static_cast<int>(std::ceil(v1)), 0, 16);
            if (x1 <= x0 || y1 <= y0)
                return false;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const int px = std::min(x, 15);
                    const int py = std::min(y, 15);
                    if (tile[(py * 16 + px) * 4 + 3] < 128u)
                        return false;
                }
            }
            return true;
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

        u32 mapped = 0, fallback = 0, skipped = 0, autoCutout = 0, shapedStates = 0;
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
                    look.flags = IsOpaqueOccluder(bname) ? kBlockFlagOpaque : 0u;
                    if (IsCutout(bname))
                        look.flags |= kBlockFlagCutout;
                    if (IsInnerFaces(bname))
                        look.flags |= kBlockFlagInner;
                    if ((look.flags & kBlockFlagCutout) == 0u && facesNeedCutout(look.faces)) {
                        look.flags |= kBlockFlagCutout;
                        look.flags &= ~kBlockFlagOpaque;
                        ++autoCutout;
                    }
                } else {
                    const json *bj = blockJson(bname);
                    const std::vector<ModelRef> refs =
                        (bj && bj->is_object()) ? CollectModels(*bj, pmap) : std::vector<ModelRef>{};
                    FaceTextures ft{};
                    std::array<std::array<float, 4>, 6> unionMin{};
                    std::array<std::array<float, 4>, 6> unionMax{};
                    std::array<bool, 6> unionHave{false, false, false, false, false, false};
                    for (const ModelRef &ref : refs) {
                        if (ref.model.empty())
                            continue;
                        FaceTextures part = ResolveModel(modelsDir, modelCache, ref.model);
                        if (!part.any)
                            continue;
                        RotateFaces(part, ref.x, ref.y);
                        for (size_t f = 0; f < 6; ++f) {
                            if (part.tex[f].empty())
                                continue;
                            if (ft.tex[f].empty()) {
                                ft.tex[f] = part.tex[f];
                                ft.uv[f] = part.uv[f];
                                unionMin[f][0] = std::min(part.uv[f][0], part.uv[f][2]);
                                unionMin[f][1] = std::min(part.uv[f][1], part.uv[f][3]);
                                unionMax[f][0] = std::max(part.uv[f][0], part.uv[f][2]);
                                unionMax[f][1] = std::max(part.uv[f][1], part.uv[f][3]);
                                unionHave[f] = true;
                            } else if (ft.tex[f] == part.tex[f] && unionHave[f]) {
                                unionMin[f][0] =
                                    std::min(unionMin[f][0], std::min(part.uv[f][0], part.uv[f][2]));
                                unionMin[f][1] =
                                    std::min(unionMin[f][1], std::min(part.uv[f][1], part.uv[f][3]));
                                unionMax[f][0] =
                                    std::max(unionMax[f][0], std::max(part.uv[f][0], part.uv[f][2]));
                                unionMax[f][1] =
                                    std::max(unionMax[f][1], std::max(part.uv[f][1], part.uv[f][3]));
                                const bool flipU = ft.uv[f][0] > ft.uv[f][2];
                                const bool flipV = ft.uv[f][1] > ft.uv[f][3];
                                ft.uv[f][0] = flipU ? unionMax[f][0] : unionMin[f][0];
                                ft.uv[f][1] = flipV ? unionMax[f][1] : unionMin[f][1];
                                ft.uv[f][2] = flipU ? unionMin[f][0] : unionMax[f][0];
                                ft.uv[f][3] = flipV ? unionMin[f][1] : unionMax[f][1];
                            }

                        }
                        ft.any = true;
                    }
                    if (!ft.any) {
                        look.flags = kBlockFlagOpaque;
                        ++fallback;
                    } else {
                        if (!refs.empty() && refs[0].model.find("cube_column") != std::string::npos) {
                            const auto ait = pmap.find("axis");
                            if (ait != pmap.end() && ait->second != "y") {
                                const size_t ax = (ait->second == "x") ? 0u : 4u;
                                const std::string endT = ft.tex[2];
                                const std::string sideT = ft.tex[ax];
                                const auto endUv = ft.uv[2];
                                const auto sideUv = ft.uv[ax];
                                ft.tex[2] = ft.tex[3] = sideT;
                                ft.uv[2] = ft.uv[3] = sideUv;
                                ft.tex[ax] = ft.tex[ax + 1] = endT;
                                ft.uv[ax] = ft.uv[ax + 1] = endUv;
                            }
                        }
                        bool fullShape = true;
                        if (!refs.empty()) {
                            const StateShape shape = CollectStateShape(modelsDir, modelCache, refs);
                            if (shape.hasElements) {
                                for (int a = 0; a < 3; ++a) {
                                    long mn = std::lround(std::clamp(shape.mn[a], 0.0, 16.0));
                                    long mx = std::lround(std::clamp(shape.mx[a], 0.0, 16.0));
                                    if (mx <= mn) {
                                        if (mx < 16)
                                            ++mx;
                                        else
                                            --mn;
                                    }
                                    look.shapeMin[a] = static_cast<u8>(mn);
                                    look.shapeMax[a] = static_cast<u8>(mx);
                                }
                                fullShape = look.shapeMin[0] == 0 && look.shapeMin[1] == 0 &&
                                            look.shapeMin[2] == 0 && look.shapeMax[0] == 16 &&
                                            look.shapeMax[1] == 16 && look.shapeMax[2] == 16;
                                if (!fullShape)
                                    ++shapedStates;
                            }
                        }
                        for (size_t f = 0; f < 6; ++f) {
                            if (ft.tex[f].empty())
                                continue;
                            const float u0 = ft.uv[f][0], v0 = ft.uv[f][1];
                            const float u1 = ft.uv[f][2], v1 = ft.uv[f][3];
                            const float rectW = std::abs(u1 - u0);
                            const float rectH = std::abs(v1 - v0);
                            if (rectW < 1e-6f || rectH < 1e-6f)
                                continue;
                            float faceW = 16.f, faceH = 16.f;
                            const int sx = static_cast<int>(look.shapeMax[0]) -
                                           static_cast<int>(look.shapeMin[0]);
                            const int sy = static_cast<int>(look.shapeMax[1]) -
                                           static_cast<int>(look.shapeMin[1]);
                            const int sz = static_cast<int>(look.shapeMax[2]) -
                                           static_cast<int>(look.shapeMin[2]);
                            if (f == 0u || f == 1u) {
                                faceW = static_cast<float>(sz);
                                faceH = static_cast<float>(sy);
                            } else if (f == 2u || f == 3u) {
                                faceW = static_cast<float>(sx);
                                faceH = static_cast<float>(sz);
                            } else {
                                faceW = static_cast<float>(sx);
                                faceH = static_cast<float>(sy);
                            }
                            if (faceW < 1e-6f || faceH < 1e-6f)
                                continue;
                            const bool rectWide = rectW > rectH + 1e-6f;
                            const bool rectTall = rectH > rectW + 1e-6f;
                            const bool faceWide = faceW > faceH + 1e-6f;
                            const bool faceTall = faceH > faceW + 1e-6f;
                            if ((rectWide && faceTall) || (rectTall && faceWide)) {

                                ft.uv[f][0] = 0.f;
                                ft.uv[f][1] = 0.f;
                                ft.uv[f][2] = rectH;
                                ft.uv[f][3] = rectW;
                            }
                        }
                        const bool isFence =
                            bname.size() > 6 &&
                            bname.compare(bname.size() - 6, 6, "_fence") == 0;
                        bool useSyntheticFence = false;
                        std::vector<Box3> fenceBoxes;
                        if (isFence && !refs.empty()) {
                            fenceBoxes = CollectSolidBoxes(modelsDir, modelCache, refs);
                            useSyntheticFence = fenceBoxes.size() > 1;
                        }
                        if (useSyntheticFence) {
                            u16 plankTile = tileForPath(ft.tex[0].empty() ? ft.tex[2] : ft.tex[0]);
                            if (plankTile == 0u || plankTile >= pack.tiles.size())
                                plankTile = tileForPath("block/oak_planks");
                            if (plankTile >= pack.tiles.size())
                                plankTile = 0u;
                            std::vector<u8> plankPx = pack.tiles[plankTile];
                            if (plankPx.size() < 16u * 16u * 4u)
                                plankPx = pack.tiles[0];
                            const int sMin[3]{look.shapeMin[0], look.shapeMin[1], look.shapeMin[2]};
                            const int sMax[3]{look.shapeMax[0], look.shapeMax[1], look.shapeMax[2]};
                            auto insideAnyBoxProj = [&](size_t f, double x, double y,
                                                        double z) -> bool {
                                for (const Box3 &b : fenceBoxes) {
                                    bool ok = false;
                                    if (f == 0u || f == 1u) {
                                        ok = y >= b.mn[1] - 1e-6 && y <= b.mx[1] + 1e-6 &&
                                             z >= b.mn[2] - 1e-6 && z <= b.mx[2] + 1e-6;
                                    } else if (f == 2u || f == 3u) {
                                        ok = x >= b.mn[0] - 1e-6 && x <= b.mx[0] + 1e-6 &&
                                             z >= b.mn[2] - 1e-6 && z <= b.mx[2] + 1e-6;
                                    } else {
                                        ok = x >= b.mn[0] - 1e-6 && x <= b.mx[0] + 1e-6 &&
                                             y >= b.mn[1] - 1e-6 && y <= b.mx[1] + 1e-6;
                                    }
                                    if (ok)
                                        return true;
                                }
                                return false;
                            };
                            const int fSx = sMax[0] - sMin[0];
                            const int fSz = sMax[2] - sMin[2];
                            auto isEndFace = [&](size_t f) -> bool {
                                if (fSx > fSz + 1e-6) {
                                    return f == 0u || f == 1u;
                                }
                                if (fSz > fSx + 1e-6) {
                                    return f == 4u || f == 5u;
                                }
                                return false;
                            };
                            for (size_t f = 0; f < 6; ++f) {
                                if (isEndFace(f)) {

                                    u16 t = plankTile;
                                    look.faces.tile[f] = t;
                                    look.uvPacked[f] = 0xFFFF0000u;
                                    continue;
                                }
                                std::vector<u8> synth(16 * 16 * 4, 0);
                                bool anySolid = false, anyHole = false;
                                for (int ty = 0; ty < 16; ++ty) {
                                    for (int tx = 0; tx < 16; ++tx) {
                                        double x = 8.0, y = 8.0, z = 8.0;
                                        if (f == 0u) {
                                            x = static_cast<double>(sMax[0]);
                                            z = sMin[2] + (tx + 0.5) / 16.0 * (sMax[2] - sMin[2]);
                                            y = sMax[1] - (ty + 0.5) / 16.0 * (sMax[1] - sMin[1]);
                                        } else if (f == 1u) {
                                            x = static_cast<double>(sMin[0]);
                                            z = sMin[2] + (tx + 0.5) / 16.0 * (sMax[2] - sMin[2]);
                                            y = sMax[1] - (ty + 0.5) / 16.0 * (sMax[1] - sMin[1]);
                                        } else if (f == 2u) {
                                            y = static_cast<double>(sMax[1]);
                                            x = sMin[0] + (tx + 0.5) / 16.0 * (sMax[0] - sMin[0]);
                                            z = sMin[2] + (ty + 0.5) / 16.0 * (sMax[2] - sMin[2]);
                                        } else if (f == 3u) {
                                            y = static_cast<double>(sMin[1]);
                                            x = sMin[0] + (tx + 0.5) / 16.0 * (sMax[0] - sMin[0]);
                                            z = sMin[2] + (ty + 0.5) / 16.0 * (sMax[2] - sMin[2]);
                                        } else if (f == 4u) {
                                            z = static_cast<double>(sMax[2]);
                                            x = sMin[0] + (tx + 0.5) / 16.0 * (sMax[0] - sMin[0]);
                                            y = sMax[1] - (ty + 0.5) / 16.0 * (sMax[1] - sMin[1]);
                                        } else {
                                            z = static_cast<double>(sMin[2]);
                                            x = sMin[0] + (tx + 0.5) / 16.0 * (sMax[0] - sMin[0]);
                                            y = sMax[1] - (ty + 0.5) / 16.0 * (sMax[1] - sMin[1]);
                                        }
                                        const bool solid = insideAnyBoxProj(f, x, y, z);
                                        u8 *d = &synth[(ty * 16 + tx) * 4];
                                        const u8 *s = &plankPx[(ty * 16 + tx) * 4];
                                        if (solid) {
                                            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
                                            anySolid = true;
                                        } else {
                                            d[0] = 0; d[1] = 0; d[2] = 0; d[3] = 0;
                                            anyHole = true;
                                        }
                                    }
                                }
                                if (!anySolid) {

                                    look.faces.tile[f] = 0u;
                                    look.uvPacked[f] = 0xFFFF0000u;
                                    continue;
                                }
                                const std::string skey =
                                    "fence/synth/" + bname + "/" + key + "/" + std::to_string(f);
                                auto it = tileByPath.find(skey);
                                u16 idx;
                                if (it != tileByPath.end()) {
                                    idx = it->second;
                                } else {
                                    idx = static_cast<u16>(pack.tiles.size());
                                    pack.tiles.push_back(std::move(synth));
                                    pack.tilePaths.push_back(skey);
                                    tileCutout.push_back(anyHole ? 1u : 0u);
                                    tileByPath[skey] = idx;
                                }
                                look.faces.tile[f] = idx;
                                look.uvPacked[f] = 0xFFFF0000u;
                            }
                        } else {
                        for (size_t f = 0; f < 6; ++f) {
                            u16 t = tileForPath(ft.tex[f]);
                            if (t != 0u && t < pack.tiles.size() && t < tileCutout.size() &&
                                tileCutout[t] && !ft.tex[f].empty()) {
                                if (rectIsOpaque(pack.tiles[t], ft.uv[f]))
                                    t = tileForOpaque(ft.tex[f], t);
                            }
                            look.faces.tile[f] = t;
                        }
                        }
                        if (!useSyntheticFence) {
                        for (size_t f = 0; f < 6; ++f) {
                            long q[4];
                            for (int k = 0; k < 4; ++k) {
                                q[k] = std::lround(std::clamp(ft.uv[f][static_cast<size_t>(k)],
                                                             0.f, 16.f) /
                                                 16.f * 255.f);
                                q[k] = std::clamp(q[k], 0L, 255L);
                            }
                            if (q[0] == q[2]) {
                                if (q[2] < 255)
                                    ++q[2];
                                else
                                    --q[0];
                            }
                            if (q[1] == q[3]) {
                                if (q[3] < 255)
                                    ++q[3];
                                else
                                    --q[1];
                            }
                            look.uvPacked[f] = static_cast<u32>(q[0]) |
                                               (static_cast<u32>(q[1]) << 8u) |
                                               (static_cast<u32>(q[2]) << 16u) |
                                               (static_cast<u32>(q[3]) << 24u);
                        }
                        }
                        u16 anyTile = 0;
                        u32 anyUv = 0xFFFF0000u;
                        for (size_t f = 0; f < 6; ++f) {
                            if (look.faces.tile[f] != 0u) {
                                anyTile = look.faces.tile[f];
                                anyUv = look.uvPacked[f];
                                break;
                            }
                        }
                        for (size_t f = 0; f < 6; ++f) {
                            if (look.faces.tile[f] == 0u) {
                                look.faces.tile[f] = anyTile;
                                look.uvPacked[f] = anyUv;
                            }
                        }
                        look.flags = (fullShape && IsOpaqueOccluder(bname)) ? kBlockFlagOpaque : 0u;
                        if (IsCutout(bname))
                            look.flags |= kBlockFlagCutout;
                        if (IsInnerFaces(bname))
                            look.flags |= kBlockFlagInner;
                        if ((look.flags & kBlockFlagCutout) == 0u &&
                            facesNeedCutout(look.faces)) {
                            look.flags |= kBlockFlagCutout;
                            look.flags &= ~kBlockFlagOpaque;
                            ++autoCutout;
                        }
                        if (anyTile == 0u)
                            ++fallback;
                        else
                            ++mapped;
                    }
                }
                pack.fluid[id] = static_cast<u8>(fluid);
            }
            pack.states[id] = look;
            pack.stateByKey.emplace(key, id);
            if (pack.stateKeys.size() <= id)
                pack.stateKeys.resize(static_cast<size_t>(id) + 1);
            pack.stateKeys[id] = key;
            ++id;
        }

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
        std::printf("[BlockAssets] states=%u mapped=%u fallback=%u skipped=%u autoCutout=%u "
                    "shaped=%u tiles=%zu (%s)\n",
                    id, mapped, fallback, skipped, autoCutout, shapedStates, out.tiles.size(),
                    assetsDir.c_str());
        return true;
    }
}
