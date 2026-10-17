#include "gpu/texture_cache.h"

#include "gpu/texture_astc.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace h3::gpu::texture
{
    namespace
    {
        struct Header
        {
            char magic[8];
            uint32_t block;
            uint32_t width;
            uint32_t height;
            uint32_t faces;
            uint32_t levels;
            uint32_t dataSize;
        };
        constexpr char kMagic[8] = { 'H', '3', 'A', 'S', 'T', 'C', '1', '\0' };

        bool MakeEntry(SDL_GPUTextureFormat format, int block, uint32_t width, uint32_t height, Decoded& out)
        {
            if (!format || !block)
                return false;
            out.format = format;
            out.width = width;
            out.height = height;
            uint32_t blocksX = (width + block - 1) / block;
            uint32_t blocksY = (height + block - 1) / block;
            out.rowTexels = blocksX * block;
            out.rows = blocksY * block;
            out.data.resize(size_t(blocksX) * blocksY * 16);
            return true;
        }
    }

    bool SaveAstcCache(const std::filesystem::path& path, uint32_t faces, uint32_t levels,
        const std::vector<Decoded>& base, const std::vector<std::vector<Decoded>>& mips)
    {
        if (path.empty() || base.empty() || base.size() != faces || mips.size() + 1 != levels)
            return false;
        uint32_t block = uint32_t(AstcBlockSize(base[0].format));
        if (!block || base[0].width == 0 || base[0].height == 0)
            return false;

        uint64_t dataSize = 0;
        for (const Decoded& face : base)
            dataSize += face.data.size();
        for (const auto& level : mips)
            for (const Decoded& face : level)
                dataSize += face.data.size();
        if (dataSize > 0xFFFFFFFFull)
            return false;

        Header header{};
        memcpy(header.magic, kMagic, sizeof(kMagic));
        header.block = block;
        header.width = base[0].width;
        header.height = base[0].height;
        header.faces = faces;
        header.levels = levels;
        header.dataSize = uint32_t(dataSize);

        std::filesystem::path temporary = path;
        temporary += ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        for (const Decoded& face : base)
            file.write(reinterpret_cast<const char*>(face.data.data()), std::streamsize(face.data.size()));
        for (const auto& level : mips)
            for (const Decoded& face : level)
                file.write(reinterpret_cast<const char*>(face.data.data()), std::streamsize(face.data.size()));
        if (!file)
            return false;
        file.close();
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    }

    bool LoadAstcCache(const std::filesystem::path& path, uint32_t faces, uint32_t levels,
        std::vector<Decoded>& base, std::vector<std::vector<Decoded>>& mips)
    {
        if (path.empty() || faces == 0 || levels == 0)
            return false;
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        Header header{};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!file || memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 ||
            header.faces != faces || header.levels != levels || header.block == 0 ||
            header.block > 12 || header.width == 0 || header.height == 0 ||
            header.width > 16384 || header.height > 16384 ||
            header.faces > 2048 || header.levels > 16)
            return false;

        SDL_GPUTextureFormat format = AstcFormat(int(header.block));
        if (!format)
            return false;

        base.resize(faces);
        for (Decoded& face : base)
        {
            if (!MakeEntry(format, int(header.block), header.width, header.height, face))
                return false;
            file.read(reinterpret_cast<char*>(face.data.data()), std::streamsize(face.data.size()));
            if (!file)
                return false;
        }

        mips.assign(levels - 1, {});
        for (uint32_t level = 1; level < levels; level++)
        {
            uint32_t width = std::max(1u, header.width >> level);
            uint32_t height = std::max(1u, header.height >> level);
            mips[level - 1].resize(faces);
            for (Decoded& face : mips[level - 1])
            {
                if (!MakeEntry(format, int(header.block), width, height, face))
                    return false;
                file.read(reinterpret_cast<char*>(face.data.data()), std::streamsize(face.data.size()));
                if (!file)
                    return false;
            }
        }
        return true;
    }
}
