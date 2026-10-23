#include "BlockAssetInternal.h"
#include "VoxelAnvilReader.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace Manro {
    namespace fs = std::filesystem;
    using namespace blockassets;

    namespace {
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
        pack.tiles.emplace_back(16 * 16 * 4, 0);
        pack.tilePaths.emplace_back("manro/transparent");
        tileCutout.push_back(1u);
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
        pack.boxTable.assign(kBlockStateMax * kBlockBoxWordsPerState, 0u);
        pack.fluid.assign(kBlockStateMax, 0);
        pack.emission.assign(kBlockStateMax, 0);
        pack.skyOpacity.assign(kBlockStateMax, 0);

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
                    if (IsLeavesName(bview))
                        look.flags |= kBlockFlagLeaf;
                    if (IsTrapdoorName(bview))
                        look.flags |= kBlockFlagTrapdoor;
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
                        if (IsChainName(bname)) {
                            const auto ait = pmap.find("axis");
                            const std::string &ax = (ait != pmap.end()) ? ait->second : "y";
                            float mn[3]{7.f, 0.f, 7.f};
                            float mx[3]{9.f, 16.f, 9.f};
                            if (ax == "x") {
                                mn[0] = 0.f;
                                mx[0] = 16.f;
                            } else if (ax == "z") {
                                mn[2] = 0.f;
                                mx[2] = 16.f;
                            }
                            for (int a = 0; a < 3; ++a) {
                                look.shapeMin[a] = static_cast<u8>(mn[a]);
                                look.shapeMax[a] = static_cast<u8>(mx[a]);
                                look.collMin[a] = mn[a] / 16.f;
                                look.collMax[a] = mx[a] / 16.f;
                            }
                            fullShape = false;
                        } else {
                            for (int a = 0; a < 3; ++a) {
                                look.collMin[a] =
                                    static_cast<float>(look.shapeMin[a]) / 16.f;
                                look.collMax[a] =
                                    static_cast<float>(look.shapeMax[a]) / 16.f;
                            }
                        }
                        if (IsFenceName(bview)) {
                            const auto oit = pmap.find("open");
                            const bool gateOpen =
                                oit != pmap.end() && oit->second == "true";
                            if (!gateOpen) {
                                look.collMin[0] = 0.f;
                                look.collMin[1] = 0.f;
                                look.collMin[2] = 0.f;
                                look.collMax[0] = 1.f;
                                look.collMax[1] = 1.5f;
                                look.collMax[2] = 1.f;
                            }
                        }
                        if (IsSignName(bview)) {
                            for (int a = 0; a < 3; ++a) {
                                look.collMin[a] =
                                    static_cast<float>(look.shapeMin[a]) / 16.f;
                                look.collMax[a] =
                                    static_cast<float>(look.shapeMax[a]) / 16.f;
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
                        const bool isFence = IsFenceName(bview);
                        bool useSyntheticFence = false;
                        bool useMultiFence = false;
                        std::vector<Box3> fenceBoxes;
                        if (isFence && !refs.empty()) {
                            fenceBoxes = CollectSolidBoxes(modelsDir, modelCache, refs);
                            if (fenceBoxes.size() > 1 &&
                                fenceBoxes.size() <= kBlockBoxMax)
                                useMultiFence = true;
                            else if (fenceBoxes.size() > 1)
                                useSyntheticFence = true;
                        }
                        u16 fencePlankTile = 0u;
                        if ((useSyntheticFence || useMultiFence) &&
                            (!ft.tex[0].empty() || !ft.tex[2].empty())) {
                            fencePlankTile =
                                tileForPath(ft.tex[0].empty() ? ft.tex[2] : ft.tex[0]);
                            if (fencePlankTile == 0u ||
                                fencePlankTile >= pack.tiles.size())
                                fencePlankTile = tileForPath("block/oak_planks");
                            if (fencePlankTile >= pack.tiles.size())
                                fencePlankTile = 0u;
                        } else if (useSyntheticFence || useMultiFence) {
                            fencePlankTile = tileForPath("block/oak_planks");
                            if (fencePlankTile >= pack.tiles.size())
                                fencePlankTile = 0u;
                        }
                        if (useMultiFence) {
                            for (size_t f = 0; f < 6; ++f) {
                                look.faces.tile[f] = fencePlankTile;
                                look.uvPacked[f] = 0xFFFF0000u;
                            }
                            struct QBox {
                                u32 mn[3]{0, 0, 0};
                                u32 mx[3]{16, 16, 16};
                            };
                            std::vector<QBox> qb;
                            qb.reserve(fenceBoxes.size());
                            for (const Box3 &b : fenceBoxes) {
                                QBox q{};
                                for (int a = 0; a < 3; ++a) {
                                    long qmn = std::lround(
                                        std::clamp(b.mn[a], 0.0, 16.0));
                                    long qmx = std::lround(
                                        std::clamp(b.mx[a], 0.0, 16.0));
                                    qmn = std::clamp(qmn, 0L, 16L);
                                    qmx = std::clamp(qmx, 0L, 16L);
                                    if (qmx <= qmn) {
                                        if (qmx < 16)
                                            ++qmx;
                                        else
                                            --qmn;
                                    }
                                    q.mn[a] = static_cast<u32>(qmn);
                                    q.mx[a] = static_cast<u32>(qmx);
                                }
                                qb.push_back(q);
                            }
                            u64 hidden = 0u;
                            for (size_t i = 0; i < qb.size(); ++i) {
                                for (size_t f = 0; f < 6; ++f) {
                                    const size_t ax = f >> 1u;
                                    const bool plus = ((f & 1u) == 0u);
                                    const double p =
                                            plus
                                                ? fenceBoxes[i].mx[ax]
                                                : fenceBoxes[i].mn[ax];
                                    for (size_t j = 0; j < qb.size(); ++j) {
                                        if (j == i)
                                            continue;
                                        if (plus) {
                                            if (!(fenceBoxes[j].mn[ax] <= p + 1e-6 &&
                                                  fenceBoxes[j].mx[ax] > p + 1e-6))
                                                continue;
                                        } else {
                                            if (!(fenceBoxes[j].mx[ax] >= p - 1e-6 &&
                                                  fenceBoxes[j].mn[ax] < p - 1e-6))
                                                continue;
                                        }
                                        bool covers = true;
                                        for (size_t b = 0; b < 3; ++b) {
                                            if (b == ax)
                                                continue;
                                            if (fenceBoxes[j].mn[b] >
                                                    fenceBoxes[i].mn[b] + 1e-6 ||
                                                fenceBoxes[j].mx[b] <
                                                    fenceBoxes[i].mx[b] - 1e-6) {
                                                covers = false;
                                                break;
                                            }
                                        }
                                        if (covers) {
                                            hidden |= 1uLL << (i * 6u + f);
                                            break;
                                        }
                                    }
                                }
                            }
                            const size_t bo =
                                static_cast<size_t>(id) * kBlockBoxWordsPerState;
                            if (bo + kBlockBoxWordsPerState <= pack.boxTable.size()) {
                                pack.boxTable[bo + 0u] =
                                    static_cast<u32>(qb.size());
                                pack.boxTable[bo + 1u] =
                                    (static_cast<u32>(fencePlankTile) & 0xFFFFu) |
                                    (static_cast<u32>(hidden & 0xFFFFu) << 16u);
                                pack.boxTable[bo + 2u] =
                                    static_cast<u32>((hidden >> 16u) & 0xFFFFFFFFu);
                                pack.boxTable[bo + 3u] =
                                    static_cast<u32>((hidden >> 48u) & 0x3Fu);
                                for (size_t i = 0; i < qb.size(); ++i) {
                                    pack.boxTable[bo + 4u + i * 2u + 0u] =
                                        qb[i].mn[0] | (qb[i].mn[1] << 8u) |
                                        (qb[i].mn[2] << 16u);
                                    pack.boxTable[bo + 4u + i * 2u + 1u] =
                                        qb[i].mx[0] | (qb[i].mx[1] << 8u) |
                                        (qb[i].mx[2] << 16u);
                                }
                            }
                        }
                        if (useSyntheticFence) {
                            u16 plankTile = fencePlankTile;
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
                            for (size_t f = 0; f < 6; ++f) {
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
                                for (int ty = 0; ty < 16; ++ty) {
                                    for (int tx = 0; tx < 16; ++tx) {
                                        u8 *d = &synth[(ty * 16 + tx) * 4];
                                        if (d[3] != 0)
                                            continue;
                                        const int dx[4] = {1, -1, 0, 0};
                                        const int dy[4] = {0, 0, 1, -1};
                                        for (int k = 0; k < 4; ++k) {
                                            const int nx = tx + dx[k], ny = ty + dy[k];
                                            if (nx < 0 || nx > 15 || ny < 0 || ny > 15)
                                                continue;
                                            const u8 *s2 = &synth[(ny * 16 + nx) * 4];
                                            if (s2[3] != 0) {
                                                d[0] = s2[0];
                                                d[1] = s2[1];
                                                d[2] = s2[2];
                                                break;
                                            }
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
                        if (!useSyntheticFence && !useMultiFence) {
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
                        if (IsChainName(bname)) {
                            const u16 chainTile = look.faces.tile[0] != 0u
                                                      ? look.faces.tile[0]
                                                      : look.faces.tile[1];
                            for (size_t f = 0; f < 6; ++f)
                                look.faces.tile[f] = chainTile;
                            look.uvPacked[0] = 0xFF300000u;
                            look.uvPacked[1] = 0xFF600030u;
                            look.uvPacked[2] = look.uvPacked[0];
                            look.uvPacked[3] = look.uvPacked[1];
                            look.uvPacked[4] = look.uvPacked[0];
                            look.uvPacked[5] = look.uvPacked[1];
                        }
                        if (useMultiFence) {
                            look.flags = kBlockFlagMulti;
                        } else {
                        look.flags = (fullShape && IsOpaqueOccluder(bname)) ? kBlockFlagOpaque : 0u;
                        if (IsFenceName(bview) || IsSignName(bview))
                            look.flags &= ~kBlockFlagOpaque;
                        if (IsCutout(bname))
                            look.flags |= kBlockFlagCutout;
                        if (IsLeavesName(bview))
                            look.flags |= kBlockFlagLeaf;
                        if (IsTrapdoorName(bview))
                            look.flags |= kBlockFlagTrapdoor;
                        if (IsInnerFaces(bname))
                            look.flags |= kBlockFlagInner;
                        if (IsChainName(bname)) {
                            const auto ait = pmap.find("axis");
                            const std::string &ax =
                                (ait != pmap.end()) ? ait->second : "y";
                            look.flags &= ~kBlockFlagOpaque;
                            look.flags |= kBlockFlagCutout;
                            if (ax == "x")
                                look.flags |= kBlockFlagCrossX;
                            else if (ax == "z")
                                look.flags |= kBlockFlagCrossZ;
                            else
                                look.flags |= kBlockFlagCrossY;
                        }
                        if ((look.flags & kBlockFlagCutout) == 0u &&
                            facesNeedCutout(look.faces)) {
                            look.flags |= kBlockFlagCutout;
                            look.flags &= ~kBlockFlagOpaque;
                            ++autoCutout;
                        }
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
            pack.emission[id] = EmissionForState(bview, pmap);
            pack.skyOpacity[id] = SkyOpacityForState(bview, look.flags);
            pack.stateByKey.emplace(key, id);
            if (pack.stateKeys.size() <= id)
                pack.stateKeys.resize(static_cast<size_t>(id) + 1);
            pack.stateKeys[id] = key;
            ++id;
        }

        pack.states[0].flags = kBlockFlagSkip;
        pack.fluid[0] = 0;
        pack.emission[0] = 0;
        pack.skyOpacity[0] = 0;
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
