#include "Editor.h"

#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Interfaces/IWindow.h>
#include <Manro/Resource/TextureLoader.h>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_freetype.h>
#include <ImGuizmo.h>

#include "IconsFontAwesome7.h"

#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

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

        constexpr const char *kDockSceneView = "Scene";
        constexpr const char *kDockConsole = "Console";
        constexpr const char *kDockOutliner = "Outliner";
        constexpr const char *kDockInspector = "Inspector";
        constexpr const char *kDockAssetBrowser = "Assets";
        constexpr const char *kDockVertToolbar = "##vtoolbar";
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
        m_Vfs = &ctx.Vfs;

        m_Renderer->SetDebugUIEnabled(false);
        m_InputManager.SetBackend(&m_InputBackend);

        SetBuf(m_PackPathBuf, sizeof(m_PackPathBuf), "scenes/test.rres");

        Manro::CLogger::SetCallback([this](Manro::LogLevel lvl, std::string_view msg) {
            std::lock_guard lk(m_LogMutex);
            if (m_LogEntries.size() > 2000)
                m_LogEntries.erase(m_LogEntries.begin());
            m_LogEntries.push_back(LogEntry{lvl, std::string(msg)});
        });

        const float dpiScale = SDL_GetWindowDisplayScale(
            static_cast<SDL_Window *>(m_Window->GetNativeHandle()));
        const float density = (dpiScale > 0.f) ? dpiScale : 1.f;
        m_flDpiScale = density;

        if (m_flDpiScale > 1.0f) {
            auto settings = m_Renderer->GetSettings();
            settings.resolutionScale = 1.0f / m_flDpiScale;
            m_Renderer->SetSettings(settings);
        }

        ImGuiIO &io = ImGui::GetIO();
        io.Fonts->Clear();

        static const ImWchar iconRanges[] = {ICON_MIN_FA7, ICON_MAX_16_FA7, 0};

        ImFontConfig baseCfg;
        baseCfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        baseCfg.RasterizerDensity = density;
        m_FontUI = io.Fonts->AddFontFromFileTTF(
            "assets/fonts/Roboto-Regular.ttf", 15.f, &baseCfg);

        ImFontConfig iconCfg;
        iconCfg.MergeMode = true;
        iconCfg.GlyphMinAdvanceX = 15.f;
        iconCfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        iconCfg.RasterizerDensity = density;
        io.Fonts->AddFontFromFileTTF(
            "assets/fonts/Font Awesome 7 Free-Solid-900.otf", 14.f, &iconCfg, iconRanges);

        ImFontConfig boldCfg;
        boldCfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        boldCfg.RasterizerDensity = density;
        m_FontBold = io.Fonts->AddFontFromFileTTF(
            "assets/fonts/Roboto-Bold.ttf", 15.f, &boldCfg);
        io.Fonts->AddFontFromFileTTF(
            "assets/fonts/Font Awesome 7 Free-Solid-900.otf", 14.f, &iconCfg, iconRanges);

        ImFontConfig tbCfg;
        tbCfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        tbCfg.RasterizerDensity = density;
        m_FontToolbarIcon = io.Fonts->AddFontFromFileTTF(
            "assets/fonts/Font Awesome 7 Free-Solid-900.otf", 20.f, &tbCfg, iconRanges);
    }

    void CEditor::OnShutdown() {
        Manro::CLogger::SetCallback(nullptr);
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

        bool wantCapture = false;
        if (m_bProjectOpen) {
            const bool rmb = m_InputManager.IsMouseButtonDown(Manro::MouseButton::Right);
            if (rmb && !m_bPrevMouseLook && m_bSceneHovered)
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

        if (m_bProjectOpen) {
            for (const auto &e: m_Map.Entities()) {
                Manro::CModel *m = GetOrLoadModel(e.modelPath);
                if (!m) continue;
                m_Renderer->DrawModel(*m, EntityMatrix(e));
            }
        }

        m_Renderer->BeginRendering();
        m_Renderer->RenderQueue();

        ImGuizmo::SetOrthographic(false);

        if (!m_bProjectOpen) {
            DrawStartScreen();
        } else {
            DrawDockSpace();
        }

        DrawProgressOverlay();

        m_Renderer->EndRendering();
    }

    void CEditor::DrawDockSpace() {
        DrawMainMenuBar();

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        const float statusBarH = 25.f;
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize({vp->WorkSize.x, vp->WorkSize.y - statusBarH});
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);

        ImGui::Begin("##MainDock", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar(3);

        DrawHorizontalToolbar();

        ImGuiID dockId = ImGui::GetID("EditorDockSpace");

        if (!m_bDockLayoutBuilt) {
            m_bDockLayoutBuilt = true;
            ImGui::DockBuilderRemoveNode(dockId);
            ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);

            ImGuiID leftToolbar, afterToolbar;
            ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.03f, &leftToolbar, &afterToolbar);

            ImGuiID rightPanel, centerArea;
            ImGui::DockBuilderSplitNode(afterToolbar, ImGuiDir_Right, 0.22f, &rightPanel, &centerArea);

            ImGuiID sceneNode, consoleNode;
            ImGui::DockBuilderSplitNode(centerArea, ImGuiDir_Down, 0.25f, &consoleNode, &sceneNode);

            ImGuiID outlinerNode, inspectorNode;
            ImGui::DockBuilderSplitNode(rightPanel, ImGuiDir_Down, 0.55f, &inspectorNode, &outlinerNode);

            ImGui::DockBuilderDockWindow(kDockVertToolbar, leftToolbar);
            ImGui::DockBuilderDockWindow(kDockSceneView, sceneNode);
            ImGui::DockBuilderDockWindow(kDockConsole, consoleNode);
            ImGui::DockBuilderDockWindow(kDockOutliner, outlinerNode);
            ImGui::DockBuilderDockWindow(kDockInspector, inspectorNode);
            ImGui::DockBuilderDockWindow(kDockAssetBrowser, consoleNode);

            ImGuiDockNode *tbNode = ImGui::DockBuilderGetNode(leftToolbar);
            if (tbNode) {
                tbNode->LocalFlags |= static_cast<ImGuiDockNodeFlags>(
                    static_cast<int>(ImGuiDockNodeFlags_NoTabBar) |
                    static_cast<int>(ImGuiDockNodeFlags_NoResize));
            }

            ImGui::DockBuilderFinish(dockId);
        }

        ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();

        DrawVerticalToolbar();
        DrawSceneView();
        DrawConsole();
        DrawOutliner();
        DrawInspector();
        DrawAssetBrowser();
        DrawPackDialog();
        DrawNewScenePopup();
        DrawStatusBar();
    }

    void CEditor::DrawMainMenuBar() {
        if (!ImGui::BeginMainMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_FA7_FILE " New scene...")) {
                m_NewSceneNameBuf[0] = '\0';
                m_bShowNewScenePopup = true;
            }
            if (ImGui::MenuItem(ICON_FA7_FOLDER_OPEN " Open map..."))
                RequestOpenFileDialog(DialogPurpose::OpenMap, m_ProjectDir,
                                      "Manro maps", "mmap");
            if (ImGui::MenuItem(ICON_FA7_FILE_IMPORT " Import Model..."))
                RequestOpenFileDialog(DialogPurpose::ImportModel, {},
                                      "3D models", "gltf;glb;obj");
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA7_FLOPPY_DISK " Save map", nullptr, false,
                                !m_CurrentMapPath.empty())) {
                if (SaveMap(m_CurrentMapPath))
                    SetStatus("Saved " + m_CurrentMapPath);
                else
                    SetStatus("Save failed");
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA7_BOX_ARCHIVE " Pack to .rres..."))
                m_bShowPackDialog = true;
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA7_XMARK " Close project")) {
                m_Renderer->WaitIdle();
                m_bProjectOpen = false;
                m_ProjectDir.clear();
                m_ProjectScenes.clear();
                m_Map.Clear();
                m_SelectedEntity = -1;
                m_SelectedLight = -1;
                m_CurrentMapPath.clear();
                m_ModelCache.clear();
                m_Renderer->SetSkybox(Manro::kInvalidTexture);
                m_SkyboxHandle = Manro::kInvalidTexture;
                m_LoadedSkyboxPath.clear();
                m_bDirty = false;
                UpdateWindowTitle();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Add")) {
            if (ImGui::MenuItem(ICON_FA7_CUBE " Entity at origin")) {
                MapEntity e;
                e.name = "Entity_" + std::to_string(m_Map.Entities().size());
                m_Map.Entities().push_back(e);
                m_SelectedEntity = static_cast<int>(m_Map.Entities().size()) - 1;
                m_SelectedLight = -1;
                MarkDirty();
            }
            if (ImGui::MenuItem(ICON_FA7_SUN " Directional Light")) {
                MapLight l;
                l.name = "Light_" + std::to_string(m_Map.Lights().size());
                l.type = 0;
                m_Map.Lights().push_back(l);
                m_SelectedLight = static_cast<int>(m_Map.Lights().size()) - 1;
                m_SelectedEntity = -1;
                MarkDirty();
            }
            if (ImGui::MenuItem(ICON_FA7_LIGHTBULB " Point Light")) {
                MapLight l;
                l.name = "PointLight_" + std::to_string(m_Map.Lights().size());
                l.type = 1;
                l.position = m_Camera.Position;
                m_Map.Lights().push_back(l);
                m_SelectedLight = static_cast<int>(m_Map.Lights().size()) - 1;
                m_SelectedEntity = -1;
                MarkDirty();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    void CEditor::DrawHorizontalToolbar() {
        if (!ImGui::BeginMenuBar()) return;

        if (ImGui::BeginMenu(ICON_FA7_FILE " Scene")) {
            if (ImGui::MenuItem(ICON_FA7_FILE " New scene...")) {
                m_NewSceneNameBuf[0] = '\0';
                m_bShowNewScenePopup = true;
            }
            if (ImGui::MenuItem(ICON_FA7_FOLDER_OPEN " Open scene..."))
                RequestOpenFileDialog(DialogPurpose::OpenMap, m_ProjectDir,
                                      "Manro maps", "mmap");
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA7_FLOPPY_DISK " Save",
                                nullptr, false, !m_CurrentMapPath.empty())) {
                if (SaveMap(m_CurrentMapPath))
                    SetStatus("Saved " + m_CurrentMapPath);
                else
                    SetStatus("Save failed");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(ICON_FA7_FILE_IMPORT " Assets")) {
            if (ImGui::MenuItem(ICON_FA7_FILE_IMPORT " Import model..."))
                RequestOpenFileDialog(DialogPurpose::ImportModel, {},
                                      "3D models", "gltf;glb;obj");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(ICON_FA7_BOX_ARCHIVE " Export")) {
            if (ImGui::MenuItem(ICON_FA7_BOX_ARCHIVE " Pack to .rres..."))
                m_bShowPackDialog = true;
            ImGui::EndMenu();
        }

        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);

        // Scene switcher
        if (!m_ProjectScenes.empty()) {
            int currentIdx = -1;
            for (int i = 0; i < (int) m_ProjectScenes.size(); ++i) {
                if ((fs::path(m_ProjectDir) / m_ProjectScenes[i]).generic_string() == m_CurrentMapPath)
                    currentIdx = i;
            }
            std::string fallbackName = m_CurrentMapPath.empty()
                                           ? "(unsaved)"
                                           : fs::path(m_CurrentMapPath).filename().string();
            const char *preview = currentIdx >= 0
                                      ? m_ProjectScenes[currentIdx].c_str()
                                      : fallbackName.c_str();

            ImGui::SetNextItemWidth(220.f);
            if (ImGui::BeginCombo(ICON_FA7_MAP "##scenes", preview)) {
                for (int i = 0; i < (int) m_ProjectScenes.size(); ++i) {
                    const bool sel = (i == currentIdx);
                    if (ImGui::Selectable(m_ProjectScenes[i].c_str(), sel)) {
                        const std::string absPath = (fs::path(m_ProjectDir) / m_ProjectScenes[i]).string();
                        OpenMap(absPath);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch scene");
        }

        ImGui::EndMenuBar();
    }

    void CEditor::DrawVerticalToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 6));
        if (ImGui::Begin(kDockVertToolbar, nullptr,
                         ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoScrollbar)) {
            const float w = ImGui::GetContentRegionAvail().x;
            const ImVec2 sz{w, w};

            if (m_FontToolbarIcon) ImGui::PushFont(m_FontToolbarIcon);

            auto ToolBtn = [&](const char *icon, bool active, const char *tooltip) {
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                bool clicked = ImGui::Button(icon, sz);
                if (active) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
                return clicked;
            };

            if (ToolBtn(ICON_FA7_ARROWS_UP_DOWN_LEFT_RIGHT, m_GizmoOp == 0, "Translate (W)")) m_GizmoOp = 0;
            if (ToolBtn(ICON_FA7_ROTATE, m_GizmoOp == 1, "Rotate (E)")) m_GizmoOp = 1;
            if (ToolBtn(ICON_FA7_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, m_GizmoOp == 2, "Scale")) m_GizmoOp = 2;

            ImGui::Separator();

            if (ToolBtn(ICON_FA7_MAGNET, m_bSnap, "Snap")) m_bSnap = !m_bSnap;

            ImGui::Separator();

            if (ToolBtn(ICON_FA7_GLOBE, m_GizmoMode == 0, "World Space")) m_GizmoMode = 0;
            if (ToolBtn(ICON_FA7_CUBE, m_GizmoMode == 1, "Local Space")) m_GizmoMode = 1;

            if (m_FontToolbarIcon) ImGui::PopFont();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void CEditor::DrawSceneView() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        bool open = ImGui::Begin(kDockSceneView);
        m_bSceneHovered = open && ImGui::IsWindowHovered();
        if (open) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x < 1.f) avail.x = 1.f;
            if (avail.y < 1.f) avail.y = 1.f;
            m_SceneViewW = avail.x;
            m_SceneViewH = avail.y;

            ImTextureID texId = reinterpret_cast<ImTextureID>(m_Renderer->GetSceneTextureId());
            if (texId) {
                ImGui::Image(texId, avail);
                ImVec2 scenePos = ImGui::GetItemRectMin();
                ImVec2 sceneSize = ImGui::GetItemRectSize();
                ImGuizmo::SetRect(scenePos.x, scenePos.y, sceneSize.x, sceneSize.y);
                ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
                DrawGizmo();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void CEditor::DrawConsole() {
        if (!ImGui::Begin(kDockConsole)) {
            ImGui::End();
            return;
        }

        if (ImGui::SmallButton("Clear")) {
            std::lock_guard lk(m_LogMutex);
            m_LogEntries.clear();
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, m_bShowLogInfo
                                                   ? ImVec4(0.15f, 0.55f, 0.15f, 1.f)
                                                   : ImVec4(0.80f, 0.80f, 0.78f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, m_bShowLogInfo
                                                 ? ImVec4(1.f, 1.f, 1.f, 1.f)
                                                 : ImVec4(0.40f, 0.40f, 0.40f, 1.f));
        if (ImGui::SmallButton("Info")) m_bShowLogInfo = !m_bShowLogInfo;
        ImGui::PopStyleColor(2);
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, m_bShowLogWarn
                                                   ? ImVec4(0.75f, 0.55f, 0.05f, 1.f)
                                                   : ImVec4(0.80f, 0.80f, 0.78f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, m_bShowLogWarn
                                                 ? ImVec4(1.f, 1.f, 1.f, 1.f)
                                                 : ImVec4(0.40f, 0.40f, 0.40f, 1.f));
        if (ImGui::SmallButton("Warn")) m_bShowLogWarn = !m_bShowLogWarn;
        ImGui::PopStyleColor(2);
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, m_bShowLogError
                                                   ? ImVec4(0.70f, 0.12f, 0.12f, 1.f)
                                                   : ImVec4(0.80f, 0.80f, 0.78f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, m_bShowLogError
                                                 ? ImVec4(1.f, 1.f, 1.f, 1.f)
                                                 : ImVec4(0.40f, 0.40f, 0.40f, 1.f));
        if (ImGui::SmallButton("Error")) m_bShowLogError = !m_bShowLogError;
        ImGui::PopStyleColor(2);
        ImGui::SameLine();

        ImGui::Checkbox("Auto-scroll", &m_bLogAutoScroll);

        ImGui::Separator();

        ImGui::BeginChild("##logscroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard lk(m_LogMutex);
            for (const auto &entry: m_LogEntries) {
                bool show = false;
                ImVec4 col;
                switch (entry.level) {
                    case Manro::LogLevel::Trace:
                        show = m_bShowLogInfo;
                        col = ImVec4(0.45f, 0.45f, 0.45f, 1.f);
                        break;
                    case Manro::LogLevel::Info:
                        show = m_bShowLogInfo;
                        col = ImVec4(0.10f, 0.10f, 0.10f, 1.f);
                        break;
                    case Manro::LogLevel::Warn:
                        show = m_bShowLogWarn;
                        col = ImVec4(0.70f, 0.50f, 0.00f, 1.f);
                        break;
                    case Manro::LogLevel::Error:
                    case Manro::LogLevel::Critical:
                        show = m_bShowLogError;
                        col = ImVec4(0.75f, 0.10f, 0.10f, 1.f);
                        break;
                }
                if (!show) continue;
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(entry.message.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (m_bLogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.f);
        ImGui::EndChild();

        ImGui::End();
    }

    static bool MatchesFilter(const std::string &name, const char *filter) {
        if (!filter[0]) return true;
        std::string lower = name;
        std::string pat = filter;
        for (auto &c: lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto &c: pat) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lower.find(pat) != std::string::npos;
    }

    void CEditor::DrawOutliner() {
        if (!ImGui::Begin(kDockOutliner)) {
            ImGui::End();
            return;
        }

        ImGui::InputTextWithHint("##Filter", ICON_FA7_MAGNIFYING_GLASS " Search...",
                                 m_OutlinerFilter, sizeof(m_OutlinerFilter));
        ImGui::Separator();

        char entHeader[64];
        std::snprintf(entHeader, sizeof(entHeader), ICON_FA7_CUBE " Entities [%d]",
                      (int) m_Map.Entities().size());
        if (ImGui::TreeNodeEx(entHeader,
                              ImGuiTreeNodeFlags_DefaultOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth)) {
            auto &ents = m_Map.Entities();
            for (int i = 0; i < (int) ents.size(); ++i) {
                if (!MatchesFilter(ents[i].name, m_OutlinerFilter)) continue;
                ImGui::PushID(i);
                const bool sel = (m_SelectedEntity == i);
                if (ImGui::Selectable(ents[i].name.c_str(), sel)) {
                    m_SelectedEntity = i;
                    m_SelectedLight = -1;
                }
                if (ImGui::BeginPopupContextItem("entctx")) {
                    if (ImGui::MenuItem(ICON_FA7_CLONE " Duplicate")) {
                        MapEntity copy = ents[i];
                        copy.name += "_copy";
                        ents.push_back(copy);
                        MarkDirty();
                    }
                    if (ImGui::MenuItem(ICON_FA7_TRASH_CAN " Delete")) {
                        ents.erase(ents.begin() + i);
                        if (m_SelectedEntity == i) m_SelectedEntity = -1;
                        else if (m_SelectedEntity > i) --m_SelectedEntity;
                        MarkDirty();
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

        char lightHeader[64];
        std::snprintf(lightHeader, sizeof(lightHeader), ICON_FA7_LIGHTBULB " Lights [%d]",
                      (int) m_Map.Lights().size());
        if (ImGui::TreeNodeEx(lightHeader,
                              ImGuiTreeNodeFlags_DefaultOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth)) {
            auto &lights = m_Map.Lights();
            for (int i = 0; i < (int) lights.size(); ++i) {
                if (!MatchesFilter(lights[i].name, m_OutlinerFilter)) continue;
                ImGui::PushID(10000 + i);
                const bool sel = (m_SelectedLight == i);
                if (ImGui::Selectable(lights[i].name.c_str(), sel)) {
                    m_SelectedLight = i;
                    m_SelectedEntity = -1;
                }
                if (ImGui::BeginPopupContextItem("lightctx")) {
                    if (ImGui::MenuItem(ICON_FA7_TRASH_CAN " Delete")) {
                        lights.erase(lights.begin() + i);
                        if (m_SelectedLight == i) m_SelectedLight = -1;
                        else if (m_SelectedLight > i) --m_SelectedLight;
                        MarkDirty();
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
        if (!ImGui::Begin(kDockInspector)) {
            ImGui::End();
            return;
        }

        if (m_SelectedEntity >= 0 &&
            m_SelectedEntity < (int) m_Map.Entities().size()) {
            MapEntity &e = m_Map.Entities()[m_SelectedEntity];

            if (m_FontBold) ImGui::PushFont(m_FontBold);
            ImGui::Text(ICON_FA7_CUBE " Entity");
            if (m_FontBold) ImGui::PopFont();
            ImGui::Separator();

            char nameBuf[256];
            SetBuf(nameBuf, sizeof(nameBuf), e.name);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                e.name = nameBuf;
                MarkDirty();
            }

            char modelBuf[512];
            SetBuf(modelBuf, sizeof(modelBuf), e.modelPath);
            if (ImGui::InputText("Model", modelBuf, sizeof(modelBuf))) {
                e.modelPath = modelBuf;
                m_ModelCache.erase(modelBuf);
                MarkDirty();
            }

            if (ImGui::DragFloat3("Position", glm::value_ptr(e.position), 1.f)) MarkDirty();
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(e.rotation), 1.f)) MarkDirty();
            if (ImGui::DragFloat3("Scale", glm::value_ptr(e.scale), 0.05f, 0.001f, 1000.f)) MarkDirty();
        } else if (m_SelectedLight >= 0 &&
                   m_SelectedLight < (int) m_Map.Lights().size()) {
            MapLight &l = m_Map.Lights()[m_SelectedLight];

            if (m_FontBold) ImGui::PushFont(m_FontBold);
            ImGui::Text(ICON_FA7_LIGHTBULB " Light");
            if (m_FontBold) ImGui::PopFont();
            ImGui::Separator();

            char nameBuf[256];
            SetBuf(nameBuf, sizeof(nameBuf), l.name);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                l.name = nameBuf;
                MarkDirty();
            }

            const char *types[] = {"Directional", "Point"};
            if (ImGui::Combo("Type", &l.type, types, IM_ARRAYSIZE(types))) MarkDirty();
            if (l.type == 1) {
                if (ImGui::DragFloat3("Position", glm::value_ptr(l.position), 1.f)) MarkDirty();
            } else {
                if (ImGui::DragFloat3("Direction", glm::value_ptr(l.direction), 0.05f)) MarkDirty();
            }
            if (ImGui::ColorEdit3("Color", glm::value_ptr(l.color))) MarkDirty();
            if (ImGui::DragFloat("Intensity", &l.intensity, 0.05f, 0.f, 100.f)) MarkDirty();
            if (l.type == 1)
                if (ImGui::DragFloat("Range", &l.range, 1.f, 1.f, 50000.f)) MarkDirty();
        } else {
            if (m_FontBold) ImGui::PushFont(m_FontBold);
            ImGui::Text(ICON_FA7_GLOBE " World Settings");
            if (m_FontBold) ImGui::PopFont();
            ImGui::Separator();

            if (ImGui::CollapsingHeader(ICON_FA7_MOUNTAIN_SUN " Environment",
                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                SetBuf(m_SkyboxPathBuf, sizeof(m_SkyboxPathBuf), m_Map.SkyboxPath());
                if (ImGui::InputText("Skybox", m_SkyboxPathBuf, sizeof(m_SkyboxPathBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    m_Map.SetSkyboxPath(m_SkyboxPathBuf);
                    ApplySkybox();
                    MarkDirty();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Apply")) {
                    m_Map.SetSkyboxPath(m_SkyboxPathBuf);
                    ApplySkybox();
                    MarkDirty();
                }
            }

            if (ImGui::CollapsingHeader(ICON_FA7_CAMERA " Editor Camera",
                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Position", glm::value_ptr(m_Camera.Position), 1.f);
                ImGui::DragFloat("Yaw", &m_Camera.Yaw, 1.f);
                ImGui::DragFloat("Pitch", &m_Camera.Pitch, 1.f);
                ImGui::DragFloat("FoV", &m_FovDeg, 0.5f, 30.f, 120.f);
            }

            if (m_bSnap && ImGui::CollapsingHeader(ICON_FA7_MAGNET " Snap",
                                                   ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Values", m_Snap, 0.5f, 0.01f, 100.f);
            }

            if (ImGui::CollapsingHeader(ICON_FA7_SLIDERS " Render",
                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                auto settings = m_Renderer->GetSettings();
                if (ImGui::SliderFloat("Resolution Scale", &settings.resolutionScale, 0.25f, 2.0f, "%.2f"))
                    m_Renderer->SetSettings(settings);

                const unsigned effW = static_cast<unsigned>(m_Window->GetWidth() * settings.resolutionScale);
                const unsigned effH = static_cast<unsigned>(m_Window->GetHeight() * settings.resolutionScale);
                ImGui::TextDisabled("Effective: %u x %u", effW, effH);
            }
        }

        ImGui::End();
    }

    void CEditor::DrawAssetBrowser() {
        if (!ImGui::Begin(kDockAssetBrowser)) {
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
                    MarkDirty();
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
            MarkDirty();
        }
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
                raw->prepared = Manro::CModel::Prepare({raw->virtualPath}, *m_Jobs, *m_Vfs);
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
                MarkDirty();
                SetStatus("Imported " + relPath);
                break;
            }
            case DialogPurpose::PackRres: SetBuf(m_PackPathBuf, sizeof(m_PackPathBuf), path);
                break;
            default: break;
        }
    }

    void CEditor::LoadProjectFile() {
        m_ProjectScenes.clear();
        const fs::path projFile = fs::path(m_ProjectDir) / "project.mproj";
        std::ifstream f(projFile);
        if (!f.is_open()) return;
        try {
            nlohmann::json j = nlohmann::json::parse(f);
            for (const auto &s: j.value("scenes", nlohmann::json::array()))
                m_ProjectScenes.push_back(s.get<std::string>());
        } catch (...) {
        }
    }

    void CEditor::SaveProjectFile() const {
        const fs::path projFile = fs::path(m_ProjectDir) / "project.mproj";
        nlohmann::json j;
        j["scenes"] = m_ProjectScenes;
        std::ofstream f(projFile);
        if (f.is_open()) f << j.dump(2);
    }

    void CEditor::AddSceneToProject(const std::string &absPath) {
        std::error_code ec;
        const std::string rel = fs::relative(absPath, m_ProjectDir, ec).generic_string();
        const std::string key = (!ec && !rel.empty()) ? rel : absPath;
        for (const auto &s: m_ProjectScenes)
            if (s == key) return;
        m_ProjectScenes.push_back(key);
        SaveProjectFile();
    }

    void CEditor::OpenProject(const std::string &dir) {
        if (!fs::is_directory(dir)) {
            SetStatus("Not a directory: " + dir);
            return;
        }

        const bool hasProjFile = fs::exists(fs::path(dir) / "project.mproj");
        bool hasMmap = false;
        if (!hasProjFile) {
            const fs::path scenesDir = fs::path(dir) / "scenes";
            if (fs::is_directory(scenesDir)) {
                for (const auto &entry : fs::directory_iterator(scenesDir)) {
                    if (entry.path().extension() == ".mmap") { hasMmap = true; break; }
                }
            }
        }

        if (!hasProjFile && !hasMmap) {
            SetStatus("Not a project: missing project.mproj and no .mmap in scenes/");
            return;
        }

        m_Renderer->WaitIdle();
        m_Map.Clear();
        m_SelectedEntity = -1;
        m_SelectedLight = -1;
        m_CurrentMapPath.clear();
        m_ModelCache.clear();
        m_LoadedSkyboxPath.clear();
        m_bDirty = false;

        m_ProjectDir = dir;
        m_Vfs->SetBaseDir(dir);
        m_bProjectOpen = true;
        LoadProjectFile();
        UpdateWindowTitle();

        bool openedScene = false;
        for (const auto &scene : m_ProjectScenes) {
            const fs::path scenePath = fs::path(m_ProjectDir) / scene;
            if (!fs::is_regular_file(scenePath)) continue;
            if (OpenMap(scenePath.string())) {
                openedScene = true;
                break;
            }
        }

        if (!openedScene) {
            const fs::path scenesDir = fs::path(dir) / "scenes";
            if (fs::is_directory(scenesDir)) {
                std::vector<fs::path> scenePaths;
                for (const auto &entry : fs::directory_iterator(scenesDir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".mmap")
                        scenePaths.push_back(entry.path());
                }
                std::sort(scenePaths.begin(), scenePaths.end());

                for (const auto &scenePath : scenePaths) {
                    if (OpenMap(scenePath.string())) {
                        openedScene = true;
                        break;
                    }
                }
            }
        }

        if (!openedScene) NewMap();
        SetStatus("Project: " + dir);
    }

    void CEditor::CreateNewProject(const std::string &dir) {
        std::error_code ec;
        fs::create_directories(fs::path(dir) / "scenes", ec);
        if (ec) {
            SetStatus("New project failed: " + ec.message());
            return;
        }
        m_ProjectDir = dir;
        m_Vfs->SetBaseDir(dir);
        m_bProjectOpen = true;
        m_ProjectScenes.clear();
        NewMap();
        SaveMap((fs::path(dir) / "scenes" / "default.mmap").string());
        SetStatus("Project: " + dir);
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
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking);

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

    void CEditor::DrawNewScenePopup() {
        if (m_bShowNewScenePopup) {
            ImGui::OpenPopup("New Scene");
            m_bShowNewScenePopup = false;
        }

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            {vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f},
            ImGuiCond_Appearing, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({320.f, 0.f}, ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal("New Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Scene name:");
            bool confirm = ImGui::InputText("##name", m_NewSceneNameBuf,
                                            sizeof(m_NewSceneNameBuf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SetItemDefaultFocus();

            const bool nameOk = m_NewSceneNameBuf[0] != '\0';
            if (!nameOk) ImGui::BeginDisabled();
            if (ImGui::Button("Create", {120.f, 0.f}) || (confirm && nameOk)) {
                const std::string path =
                (fs::path(m_ProjectDir) / "scenes" /
                 (std::string(m_NewSceneNameBuf) + ".mmap")).string();
                NewMap();
                if (SaveMap(path))
                    SetStatus("Created " + std::string(m_NewSceneNameBuf));
                else
                    SetStatus("Save failed");
                ImGui::CloseCurrentPopup();
            }
            if (!nameOk) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {120.f, 0.f}))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void CEditor::DrawPackDialog() {
        if (!m_bShowPackDialog) return;
        ImGui::SetNextWindowSize({500.f, 0.f}, ImGuiCond_Appearing);
        if (!ImGui::Begin("Pack to .rres", &m_bShowPackDialog,
                          ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_NoDocking)) {
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
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoDocking);

        const float t = static_cast<float>(ImGui::GetTime());
        const float frac = 0.5f + 0.5f * std::sin(t * 3.f);
        ImGui::Text("Loading %d asset%s...", pending, pending == 1 ? "" : "s");
        ImGui::ProgressBar(frac, ImVec2{360.f, 0.f}, "");

        ImGui::End();
    }

    void CEditor::DrawStatusBar() {
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        const float barH = 25.f;
        ImGui::SetNextWindowPos({vp->Pos.x, vp->Pos.y + vp->Size.y - barH});
        ImGui::SetNextWindowSize({vp->Size.x, barH});

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
        ImGui::Begin("##StatusBar", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::Text("Manro Editor  " ICON_FA7_GAUGE_HIGH "  %.0f FPS  " ICON_FA7_CUBES "  %zu objects",
                    ImGui::GetIO().Framerate, m_Map.Entities().size());

        if (m_StatusTimer > 0.f && !m_StatusLine.empty()) {
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(m_StatusLine.c_str()).x - 16.f);
            float alpha = std::min(m_StatusTimer, 1.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.17f, 0.34f, 0.59f, alpha));
            ImGui::TextUnformatted(m_StatusLine.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void CEditor::NewMap() {
        m_Renderer->WaitIdle();
        m_Map.Clear();
        m_SelectedEntity = -1;
        m_SelectedLight = -1;
        m_CurrentMapPath.clear();
        m_ModelCache.clear();
        m_Renderer->SetSkybox(Manro::kInvalidTexture);
        m_SkyboxHandle = Manro::kInvalidTexture;
        m_LoadedSkyboxPath.clear();
        m_bDirty = false;
        UpdateWindowTitle();
        SetStatus("New map");
    }

    bool CEditor::OpenMap(const std::string &path) {
        if (!m_Map.LoadFromFile(path)) {
            SetStatus("Open failed: " + path);
            return false;
        }
        m_Renderer->WaitIdle();
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
            m_Vfs->SetBaseDir(root);
            m_bProjectOpen = true;
        }
        if (m_bProjectOpen) AddSceneToProject(path);
        ApplySkybox();
        m_bDirty = false;
        UpdateWindowTitle();
        SetStatus("Loaded " + path);
        return true;
    }

    bool CEditor::SaveMap(const std::string &path) {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        if (!m_Map.SaveToFile(path)) return false;
        m_CurrentMapPath = path;
        m_bDirty = false;
        UpdateWindowTitle();
        if (m_bProjectOpen) AddSceneToProject(path);
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
        if (path.empty()) {
            m_Renderer->SetSkybox(Manro::kInvalidTexture);
            m_SkyboxHandle = Manro::kInvalidTexture;
            m_LoadedSkyboxPath.clear();
            return;
        }
        if (path == m_LoadedSkyboxPath) return;

        auto faces = Manro::CTextureLoader::LoadCubemap(path, *m_Vfs);
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

    void CEditor::MarkDirty() {
        if (!m_bDirty) {
            m_bDirty = true;
            UpdateWindowTitle();
        }
    }

    void CEditor::UpdateWindowTitle() {
        std::string title = "Manro Map Editor";
        if (!m_CurrentMapPath.empty()) {
            title += " - " + fs::path(m_CurrentMapPath).filename().string();
        }
        if (m_bDirty) title += " *";
        m_Window->SetTitle(title);
    }
} // namespace ManroEdit
