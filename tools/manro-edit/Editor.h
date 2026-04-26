#pragma once

#include <Manro/Interfaces/IApplication.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Core/Logger.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/InputBackend.h>
#include <Manro/Render/Model.h>
#include <Manro/Render/Renderer.h>

#include "FlyCamera.h"
#include "Map.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ManroEdit {
    struct AsyncModelLoad {
        std::string virtualPath;
        Manro::CModel::PreparedAssets_t prepared;
        std::atomic<bool> done{false};
        std::atomic<bool> success{false};
    };

    // SDL dialog callback fires on a worker thread
    struct PendingDialogResult {
        std::atomic<bool> ready{false};
        std::mutex mtx;
        std::string path;

        void Reset() {
            ready.store(false);
            std::lock_guard l(mtx);
            path.clear();
        }

        void Deliver(std::string p) {
            {
                std::lock_guard l(mtx);
                path = std::move(p);
            }
            ready.store(true);
        }

        std::string Take() {
            std::lock_guard l(mtx);
            std::string out = std::move(path);
            path.clear();
            ready.store(false);
            return out;
        }
    };

    enum class DialogPurpose {
        None,
        OpenProject,
        NewProject,
        OpenMap,
        SaveMap,
        ImportModel,
        PackRres,
    };

    enum class StartScreenChoice { None, OpenProject, NewProject, OpenMap, Quit };

    struct LogEntry {
        Manro::LogLevel level;
        std::string message;
    };

    class CEditor final : public Manro::IApplication {
    public:
        CEditor() = default;

        ~CEditor() override = default;

        Manro::WindowDesc_t GetWindowDesc() const override;

        void OnStartup(const Manro::InitContext_t &ctx) override;

        void OnShutdown() override;

        bool OnUpdate(const Manro::FrameContext_t &ctx,
                      const Manro::UserCmd_t &cmd) override;

        void OnRender(Manro::FrameContext_t &frame) override;

        Manro::CInputManager *GetInputManager() override { return &m_InputManager; }

    private:
        Manro::CModel *GetOrLoadModel(const std::string &path);

        void DrainLoadQueue();

        static Manro::Mat4 EntityMatrix(const MapEntity &e);

        static void DecomposeMatrix(const Manro::Mat4 &m, Manro::Vec3 &t, Manro::Vec3 &r, Manro::Vec3 &s);

        void DrawDockSpace();

        void DrawMainMenuBar();

        void DrawHorizontalToolbar();

        void DrawVerticalToolbar();

        void DrawSceneView();

        void DrawConsole();

        void DrawOutliner();

        void DrawInspector();

        void DrawAssetBrowser();

        void DrawGizmo();

        void DrawPackDialog();

        void DrawProgressOverlay();

        void DrawStartScreen();

        void OpenProject(const std::string &dir);

        void CreateNewProject(const std::string &dir);

        void NewMap();

        void OpenMap(const std::string &path);

        bool SaveMap(const std::string &path);

        std::string ImportModelIntoProject(const std::string &absModelPath);

        void ApplySkybox();

        bool PackToRres(const std::string &outRres, std::string &outError) const;

        void RequestOpenFolderDialog(DialogPurpose purpose,
                                     const std::string &startDir);

        void RequestOpenFileDialog(DialogPurpose purpose,
                                   const std::string &startDir,
                                   const char *filterName,
                                   const char *filterPattern);

        void RequestSaveFileDialog(DialogPurpose purpose,
                                   const std::string &startDir,
                                   const char *filterName,
                                   const char *filterPattern);

        void HandleDialogResult(const std::string &path);

        Manro::IWindow *m_Window = nullptr;
        Manro::CJobSystem *m_Jobs = nullptr;
        Manro::CRenderer *m_Renderer = nullptr;

        Manro::CInputBackend m_InputBackend;
        Manro::CInputManager m_InputManager;

        FlyCamera_t m_Camera;
        bool m_bMouseLook = false;
        bool m_bPrevMouseLook = false;
        bool m_bWindowCaptured = false;
        bool m_bSceneHovered = false;

        CMap m_Map;
        int m_SelectedEntity = -1;
        int m_SelectedLight = -1;
        std::string m_CurrentMapPath;

        bool m_bProjectOpen = false;
        std::string m_ProjectDir;

        struct CacheEntry {
            Manro::Scope<Manro::CModel> model;
            Manro::Scope<AsyncModelLoad> async;
        };

        std::unordered_map<std::string, CacheEntry> m_ModelCache;

        DialogPurpose m_DialogPurpose = DialogPurpose::None;
        PendingDialogResult m_DialogResult;

        char m_PackPathBuf[512]{};
        char m_SkyboxPathBuf[512]{};
        std::string m_LoadedSkyboxPath;
        Manro::TextureHandle m_SkyboxHandle{};

        bool m_bShowPackDialog = false;
        std::string m_StatusLine;
        float m_StatusTimer = 0.f;

        int m_GizmoOp = 0;
        int m_GizmoMode = 0;
        bool m_bSnap = false;
        float m_Snap[3]{1.f, 1.f, 1.f};

        float m_FovDeg = 70.f;
        float m_NearZ = 1.f;
        float m_FarZ = 20000.f;

        bool m_bDockLayoutBuilt = false;

        float m_SceneViewW = 800.f;
        float m_SceneViewH = 600.f;

        std::vector<LogEntry> m_LogEntries;
        std::mutex m_LogMutex;
        bool m_bLogAutoScroll = true;
        bool m_bShowLogInfo = true;
        bool m_bShowLogWarn = true;
        bool m_bShowLogError = true;

        void SetStatus(std::string msg, float seconds = 3.f);
    };
} // namespace ManroEdit
