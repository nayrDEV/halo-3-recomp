// Persistent ASTC texture cache. One file holds every face/layer and mip
// level of one guest texture, keyed by the texture's content hash + layout.

#pragma once

#include "gpu/texture.h"

#include <filesystem>
#include <vector>

namespace h3::gpu::texture
{
    bool SaveAstcCache(const std::filesystem::path& path, uint32_t faces, uint32_t levels,
        const std::vector<Decoded>& base, const std::vector<std::vector<Decoded>>& mips);

    // Loads a cache written by SaveAstcCache. base/mips are sized from the
    // header and filled with ASTC entries.
    bool LoadAstcCache(const std::filesystem::path& path, uint32_t faces, uint32_t levels,
        std::vector<Decoded>& base, std::vector<std::vector<Decoded>>& mips);
}
