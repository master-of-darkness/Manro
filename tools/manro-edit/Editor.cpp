#include "Editor.h"

#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Interfaces/IWindow.h>
#include <Manro/Resource/TextureLoader.h>

#include <SDL3/SDL_dialog.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace ManroEdit {
    namespace {
        void SetBuf(char *dst, size_t cap, const std::string &s) {
            const size_t n = std::min(s.size(), cap - 1);
            std::memcpy(dst, s.data(), n);
            dst[n] = '\0';
        }

        void SDLCALL OnSdlDialogResult(void *ud, const char *const *files,
                                       int /*filter*/) {
            auto *mb = static_cast<PendingDialogResult *>(ud);
            if (!mb) return;
            mb->Deliver(files && files[0] ? std::string(files[0]) : std::string{});
        }
    } // namespace

    Manro::WindowDesc_t CEditor::GetWindowDesc() const {
        Manro::WindowDesc_t d;
        d.Title = "Manro Map Editor";
        d.Width = 1600;
        d.Height = 900;
        d.Resizable = true;
        return d;
    }

    void CEditor::OnStartup(const Manro::InitContext_t &ctx) {
        m_Window = &ctx.CWindow;
        m_Jobs = &ctx.Jobs;
        m_Renderer = &ctx.CRenderer;

        m_Renderer->SetDebugUIEnabled(false);
        m_InputManager.SetBackend(&m_InputBackend);

        SetBuf(m_PackPathBuf, sizeof(m_PackPathBuf), "scenes/test.rres");
    }

    void CEditor::OnShutdown() {
        m_ModelCache.clear();
    }

    bool CEditor::OnUpdate(const Manro::FrameContext_t &ctx,
                           const Manro::UserCmd_t & /*cmd*/) {
        const float dt = ctx.DeltaTime;

        if (m_StatusTimer > 0.f) m_StatusTimer -= dt;

        if (m_DialogResult.ready.load()) {
            HandleDialogResult(m_DialogResult.Take());
        }

        DrainLoadQueue();

        // Don't pump the camera while ImGui owns the mouse
        bool wantCapture = false;
        if (m_bProjectOpen) {
            const bool rmb = m_InputManager.IsMouseButtonDown(Manro::MouseButton::Right);
            if (rmb && !m_bPrevMouseLook && !ImGui::GetIO().WantCaptureMouse)
                m_bMouseLook = true;
            if (!rmb) m_bMouseLook = false;
            m_bPrevMouseLook = rmb;
            wantCapture = m_bMouseLook;

            if (m_bMouseLook) m_Camera.Update(m_InputManager, dt);
            else m_InputManager.ConsumeMouseDelta();

            if (!ImGui::GetIO().WantCaptureKeyboard) {
                if (m_InputManager.IsKeyDown(Manro::Key::W)) m_GizmoOp = 0;
                if (m_InputManager.IsKeyDown(Manro::Key::E)) m_GizmoOp = 1;
                if (m_InputManager.IsKeyDown(Manro::Key::Escape)) {
                    m_SelectedEntity = -1;
                    m_SelectedLight = -1;
                }
            }
        } else {
            m_InputManager.ConsumeMouseDelta();
        }

        // Re-applying SDL relative-mouse-mode every frame eats events the
        // imgui_impl_sdl3 backend depends on; only push when state flips
        if (wantCapture != m_bWindowCaptured) {
            m_Window->CaptureMouse(wantCapture);
            m_Window->ShowCursor(!wantCapture);
            m_bWindowCaptured = wantCapture;
        }

        return true;
    }

    void CEditor::OnRender(Manro::FrameContext_t &/*frame*/) {
        const Manro::Mat4 view = m_Camera.View();
        const Manro::Mat4 proj = FlyCamera_t::Projection(
            m_FovDeg, m_Renderer->GetAspectRatio(), m_NearZ, m_FarZ);

        m_Renderer->SetViewProjection(view, proj);
        m_Renderer->SetCameraPosition(m_Camera.Position);

        m_Renderer->ClearLights();
        if (m_Map.Lights().empty()) {
            Manro::LightData sun{};
            sun.type = shaderio::eLightTypeDirectional;
            sun.direction = glm::normalize(Manro::Vec3{0.4f, -1.f, 0.2f});
            sun.color = {1.f, 0.97f, 0.92f};
            sun.intensity = 3.f;
            m_Renderer->AddLight(sun);
        } else {
            for (const auto &l: m_Map.Lights()) {
                Manro::LightData ld{};
                ld.type = (l.type == 0)
                              ? shaderio::eLightTypeDirectional
                              : shaderio::eLightTypePoint;
                ld.position = l.position;
                ld.direction = glm::normalize(l.direction);
                ld.color = l.color;
                ld.intensity = l.intensity;
                if (l.type != 0)
                    ld.angularSizeOrInvRange = 1.f / std::max(l.range, 0.001f);
                m_Renderer->AddLight(ld);
            }
        }

        // Map entities can move at edit time, so DrawModel not
        // DrawModelStatic; the runtime promotes them to static draws
        if (m_bProjectOpen) {
            for (const auto &e: m_Map.Entities()) {
                Manro::CModel *m = GetOrLoadModel(e.modelPath);
                if (!m) continue;
                m_Renderer->DrawModel(*m, EntityMatrix(e));
            }
        }

        m_Renderer->BeginRendering();
        m_Renderer->RenderQueue();

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y);

        if (!m_bProjectOpen) {
            DrawStartScreen();
        } else {
            DrawMainMenuBar();
            DrawToolbar();
            DrawOutliner();
            DrawInspector();
            DrawAssetBrowser();
            DrawPackDialog();
            DrawGizmo();
        }

        DrawProgressOverlay();

        if (m_StatusTimer > 0.f && !m_StatusLine.empty()) {
            ImGui::SetNextWindowPos({
                vp->Pos.x + 10.f,
                vp->Pos.y + vp->Size.y - 32.f
            });
            ImGui::SetNextWindowBgAlpha(0.6f);
            ImGui::Begin("##status", nullptr,
                         ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextUnformatted(m_StatusLine.c_str());
            ImGui::End();
        }

        m_Renderer->EndRendering();
    }

    Manro::CModel *CEditor::GetOrLoadModel(const std::string &path) {
        if (path.empty()) return nullptr;
        auto it = m_ModelCache.find(path);
        if (it != m_ModelCache.end()) return it->second.model.get();

        auto job = Manro::CreateScope<AsyncModelLoad>();
        job->virtualPath = path;
        AsyncModelLoad *raw = job.get();

        m_ModelCache.emplace(path, CacheEntry{nullptr, std::move(job)});

        m_Jobs->Execute([raw, this]() {
            try {
                raw->prepared = Manro::CModel::Prepare({raw->virtualPath}, *m_Jobs);
                raw->success.store(!raw->prepared.subMeshes.empty());
            } catch (const std::exception &ex) {
                LOG_ERROR("[Editor] Prepare threw for {}: {}", raw->virtualPath, ex.what());
                raw->success.store(false);
            }
            raw->done.store(true);
        });

        return nullptr;
    }

    void CEditor::DrainLoadQueue() {
        // One Commit per frame keeps GPU upload spikes bounded on big maps
        for (auto &kv: m_ModelCache) {
            CacheEntry &ce = kv.second;
            if (!ce.async || !ce.async->done.load()) continue;

            if (ce.async->success.load()) {
                auto models = Manro::CModel::CommitPrepared(
                    std::move(ce.async->prepared), *m_Renderer);
                if (!models.empty() && models[0]) {
                    ce.model = std::move(models[0]);
                } else {
                    LOG_ERROR("[Editor] Commit produced no model for {}",
                              ce.async->virtualPath);
                }
            } else {
                LOG_ERROR("[Editor] Failed to load {}", ce.async->virtualPath);
            }
            ce.async.reset();
            return;
        }
    }

    Manro::Mat4 CEditor::EntityMatrix(const MapEntity &e) {
        return Manro::CMap::ComposeEntityTransform(e);
    }

    void CEditor::DecomposeMatrix(const Manro::Mat4 &m,
                                  Manro::Vec3 &t, Manro::Vec3 &r, Manro::Vec3 &s) {
        float tt[3], rr[3], ss[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(m), tt, rr, ss);
        t = {tt[0], tt[1], tt[2]};
        r = {rr[0], rr[1], rr[2]};
        s = {ss[0], ss[1], ss[2]};
    }

    void CEditor::RequestOpenFolderDialog(DialogPurpose purpose,
                                          const std::string &startDir) {
        if (m_DialogPurpose != DialogPurpose::None) return;
        m_DialogPurpose = purpose;
        m_DialogResult.Reset();
        SDL_ShowOpenFolderDialog(OnSdlDialogResult, &m_DialogResult,
                                 static_cast<SDL_Window *>(m_Window->GetNativeHandle()),
                                 startDir.empty() ? nullptr : startDir.c_str(),
                                 false);
    }

    void CEditor::RequestOpenFileDialog(DialogPurpose purpose,
                                        const std::string &startDir,
                                        const char *filterName,
                                        const char *filterPattern) {
        if (m_DialogPurpose != DialogPurpose::None) return;
        m_DialogPurpose = purpose;
        m_DialogResult.Reset();
        SDL_DialogFileFilter filter{filterName, filterPattern};
        SDL_ShowOpenFileDialog(OnSdlDialogResult, &m_DialogResult,
                               static_cast<SDL_Window *>(m_Window->GetNativeHandle()),
                               &filter, 1,
                               startDir.empty() ? nullptr : startDir.c_str(),
                               false);
    }

    void CEditor::RequestSaveFileDialog(DialogPurpose purpose,
                                        const std::string &startDir,
                                        const char *filterName,
                                        const char *filterPattern) {
        if (m_DialogPurpose != DialogPurpose::None) return;
        m_DialogPurpose = purpose;
        m_DialogResult.Reset();
        SDL_DialogFileFilter filter{filterName, filterPattern};
        SDL_ShowSaveFileDialog(OnSdlDialogResult, &m_DialogResult,
                               static_cast<SDL_Window *>(m_Window->GetNativeHandle()),
                               &filter, 1,
                               startDir.empty() ? nullptr : startDir.c_str());
    }

    void CEditor::HandleDialogResult(const std::string &path) {
        const DialogPurpose purpose = m_DialogPurpose;
        m_DialogPurpose = DialogPurpose::None;

        if (path.empty()) {
            if (purpose != DialogPurpose::None) SetStatus("Cancelled.");
            return;
        }
        switch (purpose) {
            case DialogPurpose::OpenProject: OpenProject(path);
                break;
            case DialogPurpose::NewProject: CreateNewProject(path);
                break;
            case DialogPurpose::OpenMap: OpenMap(path);
                break;
            case DialogPurpose::SaveMap:
                if (SaveMap(path)) SetStatus("Saved " + path);
                else SetStatus("Save failed");
                break;
            case DialogPurpose::ImportModel: {
                std::string relPath = ImportModelIntoProject(path);
                if (relPath.empty()) {
                    SetStatus("Import failed");
                    break;
                }
                MapEntity e;
                e.modelPath = relPath;
                e.name = fs::path(relPath).stem().string();
                if (e.name.empty()) e.name = "Entity";
                e.position = m_Camera.Position + m_Camera.Forward() * 200.f;
                m_Map.Entities().push_back(e);
                m_SelectedEntity = static_cast<int>(m_Map.Entities().size()) - 1;
                m_SelectedLight = -1;
                SetStatus("Imported " + relPath);
                break;
            }
            case DialogPurpose::PackRres: SetBuf(m_PackPathBuf, sizeof(m_PackPathBuf), path);
                break;
            default: break;
        }
    }

    void CEditor::OpenProject(const std::string &dir) {
        if (!fs::is_directory(dir)) {
            SetStatus("Not a directory: " + dir);
            return;
        }
        m_ProjectDir = dir;
        Manro::CVirtualFS::Get().SetBaseDir(dir);
        m_bProjectOpen = true;

        const fs::path autoMap = fs::path(dir) / "scenes" / "test.mmap";
        if (fs::exists(autoMap)) OpenMap(autoMap.string());
        else NewMap();
        SetStatus("Project: " + dir);
    }

    void CEditor::CreateNewProject(const std::string &dir) {
        std::error_code ec;
        fs::create_directories(fs::path(dir) / "scenes", ec);
        if (ec) {
            SetStatus("New project failed: " + ec.message());
            return;
        }
        OpenProject(dir);
        SaveMap((fs::path(dir) / "scenes" / "test.mmap").string());
    }

    void CEditor::DrawStartScreen() {
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            {vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f},
            ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({480.f, 0.f}, ImGuiCond_Always);
        ImGui::Begin("Manro Map Editor", nullptr,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

        ImGui::TextWrapped(
            "Welcome to Editor!");
        ImGui::Spacing();

        const ImVec2 btn{-1.f, 38.f};
        if (ImGui::Button("Open existing project...", btn))
            RequestOpenFolderDialog(DialogPurpose::OpenProject, {});
        if (ImGui::Button("Create new project...", btn))
            RequestOpenFolderDialog(DialogPurpose::NewProject, {});
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Open .mmap directly...", btn))
            RequestOpenFileDialog(DialogPurpose::OpenMap, {},
                                  "Manro maps", "mmap");

        ImGui::End();
    }

    void CEditor::DrawMainMenuBar() {
        if (!ImGui::BeginMainMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New map")) NewMap();
            if (ImGui::MenuItem("Open map..."))
                RequestOpenFileDialog(DialogPurpose::OpenMap, m_ProjectDir,
                                      "Manro maps", "mmap");
            if (ImGui::MenuItem("Import Model..."))
                RequestOpenFileDialog(DialogPurpose::ImportModel, {},
                                      "3D models", "gltf;glb;obj");
            ImGui::Separator();
            if (ImGui::MenuItem("Save map", nullptr, false,
                                !m_CurrentMapPath.empty())) {
                if (SaveMap(m_CurrentMapPath))
                    SetStatus("Saved " + m_CurrentMapPath);
                else
                    SetStatus("Save failed");
            }
            if (ImGui::MenuItem("Save map as..."))
                RequestSaveFileDialog(DialogPurpose::SaveMap, m_ProjectDir,
                                      "Manro maps", "mmap");
            ImGui::Separator();
            if (ImGui::MenuItem("Pack to .rres..."))
                m_bShowPackDialog = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Close project")) {
                m_bProjectOpen = false;
                m_ProjectDir.clear();
                m_Map.Clear();
                m_ModelCache.clear();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Add")) {
            if (ImGui::MenuItem("Entity at origin")) {
                MapEntity e;
                e.name = "Entity_" + std::to_string(m_Map.Entities().size());
                m_Map.Entities().push_back(e);
                m_SelectedEntity = static_cast<int>(m_Map.Entities().size()) - 1;
                m_SelectedLight = -1;
            }
            if (ImGui::MenuItem("Directional Light")) {
                MapLight l;
                l.name = "Light_" + std::to_string(m_Map.Lights().size());
                l.type = 0;
                m_Map.Lights().push_back(l);
                m_SelectedLight = static_cast<int>(m_Map.Lights().size()) - 1;
                m_SelectedEntity = -1;
            }
            if (ImGui::MenuItem("Point Light")) {
                MapLight l;
                l.name = "PointLight_" + std::to_string(m_Map.Lights().size());
                l.type = 1;
                l.position = m_Camera.Position;
                m_Map.Lights().push_back(l);
                m_SelectedLight = static_cast<int>(m_Map.Lights().size()) - 1;
                m_SelectedEntity = -1;
            }
            ImGui::EndMenu();
        }

        ImGui::TextDisabled("|");
        ImGui::TextDisabled("RMB = mouse look  |  W/E = translate/rotate gizmo");
        ImGui::EndMainMenuBar();
    }

    void CEditor::DrawToolbar() {
        ImGui::SetNextWindowPos({10.f, 30.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({0.f, 0.f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const char *ops[] = {"Translate", "Rotate", "Scale"};
            const char *modes[] = {"World", "Local"};
            ImGui::SetNextItemWidth(110.f);
            ImGui::Combo("Op", &m_GizmoOp, ops, IM_ARRAYSIZE(ops));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.f);
            ImGui::Combo("Space", &m_GizmoMode, modes, IM_ARRAYSIZE(modes));
            ImGui::SameLine();
            ImGui::Checkbox("Snap", &m_bSnap);
            if (m_bSnap) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160.f);
                ImGui::InputFloat3("##snap", m_Snap, "%.2f");
            }
        }
        ImGui::End();
    }

    void CEditor::DrawOutliner() {
        ImGui::SetNextWindowPos({10.f, 100.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({280.f, 360.f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Outliner")) {
            ImGui::End();
            return;
        }

        if (ImGui::TreeNodeEx("Entities",
                              ImGuiTreeNodeFlags_DefaultOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth)) {
            auto &ents = m_Map.Entities();
            for (int i = 0; i < (int) ents.size(); ++i) {
                ImGui::PushID(i);
                const bool sel = (m_SelectedEntity == i);
                if (ImGui::Selectable(ents[i].name.c_str(), sel)) {
                    m_SelectedEntity = i;
                    m_SelectedLight = -1;
                }
                if (ImGui::BeginPopupContextItem("entctx")) {
                    if (ImGui::MenuItem("Duplicate")) {
                        MapEntity copy = ents[i];
                        copy.name += "_copy";
                        ents.push_back(copy);
                    }
                    if (ImGui::MenuItem("Delete")) {
                        ents.erase(ents.begin() + i);
                        if (m_SelectedEntity == i) m_SelectedEntity = -1;
                        else if (m_SelectedEntity > i) --m_SelectedEntity;
                        ImGui::EndPopup();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Lights",
                              ImGuiTreeNodeFlags_DefaultOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth)) {
            auto &lights = m_Map.Lights();
            for (int i = 0; i < (int) lights.size(); ++i) {
                ImGui::PushID(10000 + i);
                const bool sel = (m_SelectedLight == i);
                if (ImGui::Selectable(lights[i].name.c_str(), sel)) {
                    m_SelectedLight = i;
                    m_SelectedEntity = -1;
                }
                if (ImGui::BeginPopupContextItem("lightctx")) {
                    if (ImGui::MenuItem("Delete")) {
                        lights.erase(lights.begin() + i);
                        if (m_SelectedLight == i) m_SelectedLight = -1;
                        else if (m_SelectedLight > i) --m_SelectedLight;
                        ImGui::EndPopup();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::IsAnyItemHovered()) {
            m_SelectedEntity = -1;
            m_SelectedLight = -1;
        }

        ImGui::End();
    }

    void CEditor::DrawInspector() {
        ImGui::SetNextWindowPos({10.f, 470.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({340.f, 320.f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Inspector")) {
            ImGui::End();
            return;
        }

        if (m_SelectedEntity >= 0 &&
            m_SelectedEntity < (int) m_Map.Entities().size()) {
            MapEntity &e = m_Map.Entities()[m_SelectedEntity];

            char nameBuf[256];
            SetBuf(nameBuf, sizeof(nameBuf), e.name);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                e.name = nameBuf;

            char modelBuf[512];
            SetBuf(modelBuf, sizeof(modelBuf), e.modelPath);
            if (ImGui::InputText("Model", modelBuf, sizeof(modelBuf))) {
                e.modelPath = modelBuf;
                m_ModelCache.erase(modelBuf);
            }

            ImGui::DragFloat3("Position", glm::value_ptr(e.position), 1.f);
            ImGui::DragFloat3("Rotation", glm::value_ptr(e.rotation), 1.f);
            ImGui::DragFloat3("Scale", glm::value_ptr(e.scale), 0.05f, 0.001f, 1000.f);
        } else if (m_SelectedLight >= 0 &&
                   m_SelectedLight < (int) m_Map.Lights().size()) {
            MapLight &l = m_Map.Lights()[m_SelectedLight];

            char nameBuf[256];
            SetBuf(nameBuf, sizeof(nameBuf), l.name);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                l.name = nameBuf;

            const char *types[] = {"Directional", "Point"};
            ImGui::Combo("Type", &l.type, types, IM_ARRAYSIZE(types));
            if (l.type == 1)
                ImGui::DragFloat3("Position", glm::value_ptr(l.position), 1.f);
            else
                ImGui::DragFloat3("Direction", glm::value_ptr(l.direction), 0.05f);
            ImGui::ColorEdit3("Color", glm::value_ptr(l.color));
            ImGui::DragFloat("Intensity", &l.intensity, 0.05f, 0.f, 100.f);
            if (l.type == 1)
                ImGui::DragFloat("Range", &l.range, 1.f, 1.f, 50000.f);
        } else {
            ImGui::TextDisabled("Nothing selected");

            ImGui::Separator();
            ImGui::Text("Map Settings");

            SetBuf(m_SkyboxPathBuf, sizeof(m_SkyboxPathBuf), m_Map.SkyboxPath());
            if (ImGui::InputText("Skybox", m_SkyboxPathBuf, sizeof(m_SkyboxPathBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_Map.SetSkyboxPath(m_SkyboxPathBuf);
                ApplySkybox();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Apply"))  {
                m_Map.SetSkyboxPath(m_SkyboxPathBuf);
                ApplySkybox();
            }

            ImGui::Separator();
            ImGui::Text("Camera");
            ImGui::DragFloat3("CamPos", glm::value_ptr(m_Camera.Position), 1.f);
            ImGui::DragFloat("CamYaw", &m_Camera.Yaw, 1.f);
            ImGui::DragFloat("CamPitch", &m_Camera.Pitch, 1.f);
            ImGui::DragFloat("FoV", &m_FovDeg, 0.5f, 30.f, 120.f);
        }

        ImGui::End();
    }

    void CEditor::DrawAssetBrowser() {
        ImGui::SetNextWindowPos({1280.f, 30.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({310.f, 400.f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Asset Browser")) {
            ImGui::End();
            return;
        }

        ImGui::TextDisabled("Project: %s", m_ProjectDir.c_str());
        ImGui::Separator();

        if (ImGui::Button("Import External Model...", ImVec2{-1.f, 0.f}))
            RequestOpenFileDialog(DialogPurpose::ImportModel, {},
                                  "3D models", "gltf;glb;obj");
        ImGui::Spacing();

        ImGui::TextUnformatted("Project files:");
        std::error_code ec;
        if (fs::is_directory(m_ProjectDir, ec)) {
            ImGui::BeginChild("##browser", ImVec2{0, 0}, true);
            for (auto it = fs::recursive_directory_iterator(
                     m_ProjectDir, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); ++it) {
                if (!it->is_regular_file()) continue;
                const std::string ext = it->path().extension().string();
                if (ext != ".gltf" && ext != ".glb" && ext != ".obj") continue;
                std::string rel = fs::relative(it->path(), m_ProjectDir, ec).generic_string();
                if (ImGui::Selectable(rel.c_str())) {
                    MapEntity e;
                    e.modelPath = rel;
                    e.name = fs::path(rel).stem().string();
                    if (e.name.empty()) e.name = "Entity";
                    e.position = m_Camera.Position + m_Camera.Forward() * 200.f;
                    m_Map.Entities().push_back(e);
                    m_SelectedEntity = static_cast<int>(m_Map.Entities().size()) - 1;
                    m_SelectedLight = -1;
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void CEditor::DrawGizmo() {
        if (m_SelectedEntity < 0 ||
            m_SelectedEntity >= (int) m_Map.Entities().size())
            return;

        MapEntity &e = m_Map.Entities()[m_SelectedEntity];

        // Skip the gizmo for failed loads
        auto cacheIt = m_ModelCache.find(e.modelPath);
        const bool failed = cacheIt != m_ModelCache.end() &&
                            !cacheIt->second.model && !cacheIt->second.async;
        if (failed) return;

        Manro::Mat4 model = EntityMatrix(e);

        const Manro::Mat4 view = m_Camera.View();
        const Manro::Mat4 proj = FlyCamera_t::Projection(
            m_FovDeg, m_Renderer->GetAspectRatio(), m_NearZ, m_FarZ);

        ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
        if (m_GizmoOp == 1) op = ImGuizmo::ROTATE;
        else if (m_GizmoOp == 2) op = ImGuizmo::SCALE;
        const ImGuizmo::MODE mode = (m_GizmoMode == 0)
                                        ? ImGuizmo::WORLD
                                        : ImGuizmo::LOCAL;

        const float *snap = m_bSnap ? m_Snap : nullptr;
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 op, mode, glm::value_ptr(model),
                                 nullptr, snap)) {
            DecomposeMatrix(model, e.position, e.rotation, e.scale);
        }
    }

    void CEditor::DrawPackDialog() {
        if (!m_bShowPackDialog) return;
        ImGui::SetNextWindowSize({500.f, 0.f}, ImGuiCond_Appearing);
        if (!ImGui::Begin("Pack to .rres", &m_bShowPackDialog,
                          ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }
        ImGui::TextWrapped(
            "Writes map.mmap, copies referenced model dirs into a temp "
            "stage, and packs that into the .rres.");
        ImGui::Separator();
        ImGui::InputText("Output", m_PackPathBuf, sizeof(m_PackPathBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse..."))
            RequestSaveFileDialog(DialogPurpose::PackRres, m_ProjectDir,
                                  "Manro rres", "rres");

        if (ImGui::Button("Pack", ImVec2{120.f, 0.f})) {
            std::string err;
            if (PackToRres(m_PackPathBuf, err)) {
                SetStatus("Packed " + std::string(m_PackPathBuf), 5.f);
                m_bShowPackDialog = false;
            } else {
                SetStatus("Pack failed: " + err, 8.f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2{120.f, 0.f}))
            m_bShowPackDialog = false;
        ImGui::End();
    }

    void CEditor::DrawProgressOverlay() {
        int pending = 0;
        for (const auto &kv: m_ModelCache)
            if (kv.second.async) ++pending;
        if (pending == 0) return;

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            {vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y - 80.f},
            ImGuiCond_Always, {0.5f, 1.f});
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("##loading", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing);

        // Prepare doesn't expose progress
        const float t = static_cast<float>(ImGui::GetTime());
        const float frac = 0.5f + 0.5f * std::sin(t * 3.f);
        ImGui::Text("Loading %d asset%s...", pending, pending == 1 ? "" : "s");
        ImGui::ProgressBar(frac, ImVec2{360.f, 0.f}, "");

        ImGui::End();
    }

    void CEditor::NewMap() {
        m_Map.Clear();
        m_SelectedEntity = -1;
        m_SelectedLight = -1;
        m_CurrentMapPath.clear();
        m_ModelCache.clear();
        m_LoadedSkyboxPath.clear();
        SetStatus("New map");
    }

    void CEditor::OpenMap(const std::string &path) {
        if (!m_Map.LoadFromFile(path)) {
            SetStatus("Open failed: " + path);
            return;
        }
        m_CurrentMapPath = path;
        m_SelectedEntity = -1;
        m_SelectedLight = -1;
        m_ModelCache.clear();

        if (!m_bProjectOpen) {
            const fs::path parent = fs::path(path).parent_path().parent_path();
            const std::string root =
                    fs::is_directory(parent)
                        ? parent.string()
                        : fs::path(path).parent_path().string();
            m_ProjectDir = root;
            Manro::CVirtualFS::Get().SetBaseDir(root);
            m_bProjectOpen = true;
        }
        ApplySkybox();
        SetStatus("Loaded " + path);
    }

    bool CEditor::SaveMap(const std::string &path) {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        if (!m_Map.SaveToFile(path)) return false;
        m_CurrentMapPath = path;
        return true;
    }

    bool CEditor::PackToRres(const std::string &outRres, std::string &outError) const {
        const fs::path outAbs = fs::path(outRres).is_absolute()
                                    ? fs::path(outRres)
                                    : fs::path(m_ProjectDir) / outRres;
        const std::string err = m_Map.PackToRres(outAbs, m_ProjectDir);
        if (!err.empty()) {
            outError = err;
            return false;
        }
        return true;
    }

    void CEditor::ApplySkybox() {
        const std::string &path = m_Map.SkyboxPath();
        if (path.empty() || path == m_LoadedSkyboxPath) return;

        auto faces = Manro::CTextureLoader::LoadCubemap(path);
        if (faces.empty()) {
            LOG_ERROR("[Editor] Failed to load skybox: {}", path);
            SetStatus("Skybox load failed: " + path);
            return;
        }
        m_SkyboxHandle = m_Renderer->UploadCubemap(faces);
        m_Renderer->SetSkybox(m_SkyboxHandle);
        m_LoadedSkyboxPath = path;
        SetStatus("Skybox: " + path);
    }

    std::string CEditor::ImportModelIntoProject(const std::string &absModelPath) {
        const fs::path src = absModelPath;
        if (!fs::is_regular_file(src)) return {};

        const fs::path srcDir = src.parent_path();
        const std::string stem = src.stem().string();

        std::error_code ec;
        fs::path rel = fs::relative(src, m_ProjectDir, ec);
        if (!ec && !rel.empty() && rel.native().rfind("..", 0) != 0) {
            return rel.generic_string();
        }

        const fs::path dstDir = fs::path(m_ProjectDir) / "models" / stem;
        fs::create_directories(dstDir, ec);
        if (ec) {
            LOG_ERROR("[Editor] Failed to create import dir {}: {}",
                      dstDir.string(), ec.message());
            return {};
        }

        for (const auto &entry : fs::directory_iterator(srcDir, ec)) {
            if (!entry.is_regular_file()) continue;
            fs::copy_file(entry.path(), dstDir / entry.path().filename(),
                          fs::copy_options::overwrite_existing, ec);
        }

        return (fs::path("models") / stem / src.filename()).generic_string();
    }

    void CEditor::SetStatus(std::string msg, float seconds) {
        m_StatusLine = std::move(msg);
        m_StatusTimer = seconds;
    }
} // namespace ManroEdit
