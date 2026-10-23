#include "BlockAssetInternal.h"

#include <algorithm>

#include <fstream>

namespace Manro::blockassets {
    namespace {
        constexpr std::array<const char *, 6> kModelDirs = {
            "east", "west", "up", "down",
            "south", "north"
        };

        template<typename T>
        static void PermuteY90(std::array<T, 6> &a) {
            std::array<T, 6> t = a;
            a[0] = std::move(t[5]);
            a[4] = std::move(t[0]);
            a[1] = std::move(t[4]);
            a[5] = std::move(t[1]);
        }

        template<typename T>
        static void PermuteX90(std::array<T, 6> &a) {
            std::array<T, 6> t = a;
            a[2] = std::move(t[5]);
            a[4] = std::move(t[2]);
            a[3] = std::move(t[4]);
            a[5] = std::move(t[3]);
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

    void RotateFaces(FaceTextures &ft, int rotX, int rotY) {
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
                for (const auto &[k, v]: pj["textures"].items()) {
                    const std::string s = TextureString(v);
                    if (!s.empty())
                        vars[k] = s;
                }
            }
        }

        std::array < bool, 6 > have{false, false, false, false, false, false};
        for (const json *ej: chainJson) {
            if (!ej->contains("elements") || !(*ej)["elements"].is_array())
                continue;
            std::array<std::string, 6> unionTex{};
            std::array < std::array < float, 4 >, 6 > unionMin{};
            std::array < std::array < float, 4 >, 6 > unionMax{};
            std::array < bool, 6 > unionHave{false, false, false, false, false, false};
            std::array < bool, 6 > unionFlipU{false, false, false, false, false, false};
            std::array < bool, 6 > unionFlipV{false, false, false, false, false, false};
            for (auto &r: unionMin)
                r = {16.f, 16.f, 16.f, 16.f};
            for (auto &r: unionMax)
                r = {0.f, 0.f, 0.f, 0.f};
            for (const auto &el: (*ej)["elements"]) {
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
                    std::array < float, 4 > rect{0.f, 0.f, 16.f, 16.f};
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
            for (bool h: have) {
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
            for (size_t f: {0u, 1u, 4u, 5u})
                need(f, "#side");
        } else if (leaf == "block/cube_top") {
            need(2, "#top");
            need(3, "#bottom");
            for (size_t f: {0u, 1u, 4u, 5u})
                need(f, "#side");
        } else {
            static const std::array<const char *, 6> kDirVars = {
                "east", "west", "up",
                "down", "south", "north"
            };
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
}
