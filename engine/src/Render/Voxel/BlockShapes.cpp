#include "BlockAssetInternal.h"

#include <algorithm>
#include <cmath>

namespace Manro::blockassets {
    namespace {
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
                for (auto &p: pts)
                    RotX90Pt(p);
            }
            for (int k = 0; k < rotY / 90; ++k) {
                for (auto &p: pts)
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
    }

    StateShape CollectStateShape(const std::string &modelsDir,
                                 std::unordered_map<std::string, json> &modelCache,
                                 const std::vector<ModelRef> &refs) {
        StateShape out{};
        for (int pass = 0; pass < 2; ++pass) {
            bool first = true;
            for (const ModelRef &ref: refs) {
                if (ref.model.empty())
                    continue;
                std::string rel = StripPrefix(ref.model);
                for (int depth = 0; depth < 9 && !rel.empty(); ++depth) {
                    const json *j = LoadModelJson(modelsDir, modelCache, rel);
                    bool found = false;
                    if (j->is_object() && j->contains("elements") &&
                        (*j)["elements"].is_array() && !(*j)["elements"].empty()) {
                        found = true;
                        for (const auto &el: (*j)["elements"]) {
                            if (!el.contains("from") || !el.contains("to"))
                                continue;
                            const auto &from = el["from"];
                            const auto &to = el["to"];
                            if (!from.is_array() || !to.is_array() || from.size() < 3 ||
                                to.size() < 3)
                                continue;
                            Box3 b{
                                {
                                    from[0].get<double>(), from[1].get<double>(),
                                    from[2].get<double>()
                                },
                                {
                                    to[0].get<double>(), to[1].get<double>(),
                                    to[2].get<double>()
                                }
                            };
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
        for (const ModelRef &ref: refs) {
            if (ref.model.empty())
                continue;
            std::string rel = StripPrefix(ref.model);
            for (int depth = 0; depth < 9 && !rel.empty(); ++depth) {
                const json *j = LoadModelJson(modelsDir, modelCache, rel);
                bool found = false;
                if (j->is_object() && j->contains("elements") &&
                    (*j)["elements"].is_array() && !(*j)["elements"].empty()) {
                    found = true;
                    for (const auto &el: (*j)["elements"]) {
                        if (!el.contains("from") || !el.contains("to"))
                            continue;
                        const auto &from = el["from"];
                        const auto &to = el["to"];
                        if (!from.is_array() || !to.is_array() || from.size() < 3 ||
                            to.size() < 3)
                            continue;
                        Box3 b{
                            {
                                from[0].get<double>(), from[1].get<double>(),
                                from[2].get<double>()
                            },
                            {
                                to[0].get<double>(), to[1].get<double>(),
                                to[2].get<double>()
                            }
                        };
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

    [[maybe_unused]] static bool HasFullCubeElement(
        const std::string &modelsDir, std::unordered_map<std::string, json> &modelCache,
        const std::string &modelPath) {
        if (modelPath.empty())
            return false;
        std::string rel = StripPrefix(modelPath);
        for (int i = 0; i < 9 && !rel.empty(); ++i) {
            const json *j = LoadModelJson(modelsDir, modelCache, rel);
            if (j->is_object() && j->contains("elements") && (*j)["elements"].is_array()) {
                for (const auto &el: (*j)["elements"]) {
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
}
