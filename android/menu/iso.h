// Minimal XDVDFS (Xbox 360 disc image) extractor, ported from
// tools/xdvdfs_extract.py. It runs on the phone after the user picks an ISO.

#pragma once

#include <filesystem>
#include <string>

namespace h3::android
{
    // Extracts isoPath into outDir. Returns an error string, empty on success.
    std::string ExtractIso(const std::filesystem::path& isoPath, const std::filesystem::path& outDir);
}
