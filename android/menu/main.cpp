// Slint launcher for the Android port. It owns the import/settings screen;
// the game itself runs in H3GameActivity (SDL3) once Play is pressed.
//
// The native side only writes settings.ini and asks Java to open the system
// file picker. All file copying/extraction happens on the Java/C++ side of
// that bridge, never on the UI thread.

#include "main.h"

#include "config/settings.h"
#include "android_bridge.h"
#include "iso.h"

#include <slint.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace
{
    std::filesystem::path g_root;
    std::optional<slint::ComponentHandle<MainWindow>> g_ui;

    void SetStatus(const std::string& text)
    {
        if (g_ui)
            (*g_ui)->set_status(slint::SharedString(text));
    }

    void SetPath(const std::string& path)
    {
        if (g_ui)
            (*g_ui)->set_game_path(slint::SharedString(path));
    }

    void RefreshGameState()
    {
        std::error_code error;
        std::filesystem::path xex = g_root / "game" / "default.xex";
        bool ready = std::filesystem::is_regular_file(xex, error);
        if (g_ui)
        {
            (*g_ui)->set_game_ready(ready);
            (*g_ui)->set_game_path(slint::SharedString(g_root.string()));
            if (!ready)
                (*g_ui)->set_status("No default.xex found. Import your own ISO or a game folder containing default.xex.");
        }
    }

    int FrameRateIndex(int rate) { return rate >= 60 ? 1 : 0; }
    int ResolutionIndex(int height)
    {
        if (height <= 360) return 0;
        if (height <= 480) return 1;
        if (height <= 720) return 2;
        return 3;
    }
    int AntiAliasingIndex(h3::config::AntiAliasing aa)
    {
        switch (aa)
        {
        case h3::config::AntiAliasing::Off: return 0;
        case h3::config::AntiAliasing::Fxaa: return 1;
        case h3::config::AntiAliasing::Smaa: return 2;
        case h3::config::AntiAliasing::Fsr: return 3;
        }
        return 1;
    }
}

namespace h3::android
{
    void OnImportFinished(const std::string& path, const std::string& error)
    {
        slint::invoke_from_event_loop([path, error] {
            if (!g_ui)
                return;
            (*g_ui)->set_importing(false);
            if (!error.empty())
            {
                SetStatus(error);
                return;
            }
            SetStatus("Game files imported: " + path);
            RefreshGameState();
        });
    }

    void OnExtractFinished(const std::string& path, const std::string& error)
    {
        slint::invoke_from_event_loop([path, error] {
            if (!g_ui)
                return;
            (*g_ui)->set_importing(false);
            if (!error.empty())
            {
                SetStatus(error);
                return;
            }
            SetPath(path);
            SetStatus("ISO extracted. Ready to play.");
            RefreshGameState();
        });
    }

    void OnSavesExported(const std::string& path, const std::string& error)
    {
        slint::invoke_from_event_loop([path, error] {
            if (!g_ui)
                return;
            (*g_ui)->set_importing(false);
            SetStatus(error.empty() ? "Saves exported." : error);
        });
    }

    void OnSavesImported(const std::string& path, const std::string& error)
    {
        slint::invoke_from_event_loop([path, error] {
            if (!g_ui)
                return;
            (*g_ui)->set_importing(false);
            SetStatus(error.empty() ? "Saves imported. Start the game to load them." : error);
        });
    }

    void OnLogExported(const std::string& path, const std::string& error)
    {
        slint::invoke_from_event_loop([path, error] {
            if (!g_ui)
                return;
            SetStatus(error.empty() ? "Log exported. Send that file with your report." : error);
        });
    }
}

#ifdef __ANDROID__
extern "C" void slint_main()
#else
int main(int argc, char** argv)
#endif
{
    g_root = h3::android::GameRoot();
    std::error_code error;
    std::filesystem::create_directories(g_root, error);

    h3::config::Settings settings;
    bool loaded = h3::config::Load(g_root / h3::config::kFileName, settings);
    if (!loaded)
        settings.debug.logToFile = true;

    auto ui = MainWindow::create();
    g_ui = ui;

    ui->set_game_ready(false);
    ui->set_game_path(slint::SharedString(g_root.string()));
    ui->set_frame_rate_index(FrameRateIndex(settings.video.frameRate));
    ui->set_resolution_index(ResolutionIndex(settings.video.outputHeight));
    ui->set_aa_index(AntiAliasingIndex(settings.video.antiAliasing));
    ui->set_touch_controls(settings.video.touchControls);
    ui->set_performance_overlay(settings.video.performanceOverlay);
    ui->set_log_to_file(true);  // debug builds: log by default, the checkbox can disable it
    ui->set_texture_cache_index(settings.video.astcBlock == 6 ? 2 : settings.video.astcBlock == 4 ? 1 : 0);
    ui->set_version(slint::SharedString("0.1.0 // arm64"));

    RefreshGameState();

    ui->on_import_iso([ui] {
        ui->set_importing(true);
        SetStatus("Choose your Halo 3 ISO (XGD2/XGD3 or a plain XISO).");
        h3::android::PickIso();
    });

    ui->on_import_folder([ui] {
        ui->set_importing(true);
        SetStatus("Choose the folder that contains default.xex (or its game/ folder).");
        h3::android::PickFolder();
    });

    ui->on_open_settings_folder([] {
        std::error_code error;
        std::filesystem::create_directories(g_root, error);
        h3::android::OpenFolder(g_root.string());
    });

    ui->on_export_saves([] {
        h3::android::ExportSaves();
        SetStatus("Choose where to save the save archive...");
    });

    ui->on_import_saves([] {
        h3::android::ImportSaves();
        SetStatus("Choose a Halo 3 save archive (.zip)...");
    });

    ui->on_export_log([] {
        h3::android::ExportLog();
        SetStatus("Preparing the log file...");
    });

    ui->on_play([ui] {
        SetStatus("Starting Halo 3...");
        h3::config::Settings settings;
        h3::config::Load(g_root / h3::config::kFileName, settings);

        settings.video.frameRate = ui->get_frame_rate_index() == 0 ? 50 : 60;
        static constexpr int kHeights[] = { 360, 480, 720, 1080 };
        settings.video.outputHeight = kHeights[std::clamp(ui->get_resolution_index(), 0, 3)];
        switch (ui->get_aa_index())
        {
        case 0: settings.video.antiAliasing = h3::config::AntiAliasing::Off; break;
        case 2: settings.video.antiAliasing = h3::config::AntiAliasing::Smaa; break;
        case 3: settings.video.antiAliasing = h3::config::AntiAliasing::Fsr; break;
        default: settings.video.antiAliasing = h3::config::AntiAliasing::Fxaa; break;
        }
        settings.video.touchControls = ui->get_touch_controls();
        settings.video.performanceOverlay = ui->get_performance_overlay();
        settings.debug.logToFile = ui->get_log_to_file();
        switch (ui->get_texture_cache_index())
        {
        case 1: settings.video.astcBlock = 4; break;
        case 2: settings.video.astcBlock = 6; break;
        default: settings.video.astcBlock = 0; break;
        }

        std::error_code error;
        std::filesystem::create_directories(g_root, error);
        h3::config::Save(g_root / h3::config::kFileName, settings);

        if (!std::filesystem::is_regular_file(g_root / "game" / "default.xex", error))
        {
            ui->set_game_ready(false);
            SetStatus("Game code is not installed. Import your own game files first.");
            return;
        }

        h3::android::StartGame();
    });

    return ui->run();
}
