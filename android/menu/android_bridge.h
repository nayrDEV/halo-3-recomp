// Thin bridge between the Slint launcher and the Android Java activity.
// Desktop builds get no-op implementations so the menu can be previewed.

#pragma once

#include <filesystem>
#include <string>

namespace h3::android
{
    std::filesystem::path GameRoot();
    void PickIso();
    void PickFolder();
    void OpenFolder(const std::string& path);
    void StartGame();
    void ExportSaves();
    void ImportSaves();
    void ExportLog();

    // Called by the Java activity / extractor when work finishes.
    void OnImportFinished(const std::string& path, const std::string& error);
    void OnExtractFinished(const std::string& path, const std::string& error);
    void OnSavesExported(const std::string& path, const std::string& error);
    void OnSavesImported(const std::string& path, const std::string& error);
    void OnLogExported(const std::string& path, const std::string& error);
}
