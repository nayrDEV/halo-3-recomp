#include "iso.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace
{
    constexpr uint64_t kSector = 2048;
    // XGD2, XGD1, XGD3 and plain XISO volume offsets.
    constexpr uint64_t kPartitions[] = { 0xFD90000, 0x18300000, 0x2080000, 0 };

    struct Entry
    {
        std::string path;
        uint32_t start = 0;
        uint32_t length = 0;
        bool directory = false;
    };

    uint16_t Read16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
    uint32_t Read32(const uint8_t* p)
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }

    bool SafeName(const std::string& name)
    {
        if (name.empty() || name == "." || name == "..")
            return false;
        return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
    }

    void Walk(std::ifstream& file, uint64_t base, uint32_t sector, uint32_t size, const std::string& path,
        std::vector<Entry>& entries)
    {
        if (size == 0 || size > 16u * 1024 * 1024)
            return;
        std::vector<uint8_t> data(size);
        file.seekg(std::streamoff(base + uint64_t(sector) * kSector));
        file.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
        if (!file)
            return;

        std::vector<uint32_t> stack{ 0 };
        std::set<uint32_t> seen;
        while (!stack.empty())
        {
            uint32_t offset = stack.back();
            stack.pop_back();
            if (seen.count(offset) || uint64_t(offset) + 14 > data.size())
                continue;
            seen.insert(offset);

            uint16_t left = Read16(data.data() + offset);
            uint16_t right = Read16(data.data() + offset + 2);
            uint32_t start = Read32(data.data() + offset + 4);
            uint32_t length = Read32(data.data() + offset + 8);
            uint8_t attributes = data[offset + 12];
            uint8_t nameLength = data[offset + 13];
            if (left == 0xFFFF && right == 0xFFFF)
                continue;
            if (uint64_t(offset) + 14 + nameLength > data.size())
                continue;

            std::string name(reinterpret_cast<const char*>(data.data() + offset + 14), nameLength);
            if (!SafeName(name))
                continue;

            if (left)
                stack.push_back(uint32_t(left) * 4);
            if (right)
                stack.push_back(uint32_t(right) * 4);

            bool directory = (attributes & 0x10) != 0;
            std::string child = path.empty() ? name : path + "/" + name;
            entries.push_back({ child, start, length, directory });
            if (directory && length)
                Walk(file, base, start, length, child, entries);
        }
    }

    uint64_t FindPartition(std::ifstream& file)
    {
        char magic[20];
        for (uint64_t offset : kPartitions)
        {
            file.seekg(std::streamoff(offset + 32 * kSector));
            file.read(magic, sizeof(magic));
            if (file && std::memcmp(magic, "MICROSOFT*XBOX*MEDIA", 20) == 0)
                return offset;
        }
        return UINT64_MAX;
    }
}

namespace h3::android
{
    std::string ExtractIso(const std::filesystem::path& isoPath, const std::filesystem::path& outDir)
    {
        std::ifstream file(isoPath, std::ios::binary);
        if (!file)
            return "Can't open the ISO: " + isoPath.string();

        uint64_t base = FindPartition(file);
        if (base == UINT64_MAX)
            return "No XDVDFS volume found. This doesn't look like an Xbox 360 game ISO.";

        file.seekg(std::streamoff(base + 32 * kSector + 20));
        uint8_t root[8];
        file.read(reinterpret_cast<char*>(root), sizeof(root));
        if (!file)
            return "Can't read the ISO volume descriptor.";
        uint32_t rootSector = Read32(root);
        uint32_t rootSize = Read32(root + 4);

        std::vector<Entry> entries;
        Walk(file, base, rootSector, rootSize, "", entries);
        if (entries.empty())
            return "The ISO's file table is empty.";

        std::error_code error;
        std::filesystem::create_directories(outDir, error);
        if (error)
            return "Can't create the game folder: " + error.message();

        for (const Entry& entry : entries)
        {
            std::filesystem::path destination = outDir / entry.path;
            if (entry.directory)
            {
                std::filesystem::create_directories(destination, error);
                if (error)
                    return "Can't create " + entry.path + ": " + error.message();
                continue;
            }

            std::filesystem::create_directories(destination.parent_path(), error);
            if (error)
                return "Can't create " + destination.parent_path().string() + ": " + error.message();

            file.seekg(std::streamoff(base + uint64_t(entry.start) * kSector));
            if (!file)
                return "Can't seek to " + entry.path;

            std::ofstream output(destination, std::ios::binary | std::ios::trunc);
            if (!output)
                return "Can't write " + destination.string();

            uint64_t left = entry.length;
            std::vector<char> buffer(1024 * 1024);
            while (left)
            {
                size_t chunk = size_t(std::min<uint64_t>(left, buffer.size()));
                file.read(buffer.data(), std::streamsize(chunk));
                if (!file)
                    return "Read failed while extracting " + entry.path;
                output.write(buffer.data(), std::streamsize(chunk));
                if (!output)
                    return "Write failed while extracting " + entry.path;
                left -= chunk;
            }
        }

        if (!std::filesystem::is_regular_file(outDir / "default.xex", error))
            return "The ISO extracted, but it has no default.xex. Use a Halo 3 Xbox 360 disc image.";

        return {};
    }
}
