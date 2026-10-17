#include "gpu/texture_astc.h"

#if defined(H3_HAVE_ASTC)

#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include <bcdec.h>

#include <astcenc.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace h3::gpu::texture
{
    namespace
    {
        struct AstcContext
        {
            astcenc_context* context = nullptr;
            ~AstcContext()
            {
                if (context)
                    astcenc_context_free(context);
            }
        };

        std::mutex g_mutex;
        AstcContext g_contexts[2];  // 4x4, 6x6

        int ContextIndex(int block) { return block == 6 ? 1 : 0; }

        astcenc_context* GetContext(int block)
        {
            int index = ContextIndex(block);
            if (g_contexts[index].context)
                return g_contexts[index].context;

            astcenc_config config{};
            astcenc_error error = astcenc_config_init(ASTCENC_PRF_LDR, unsigned(block), unsigned(block), 1,
                ASTCENC_PRE_FAST, 0, &config);
            if (error != ASTCENC_SUCCESS)
                return nullptr;
            if (astcenc_context_alloc(&config, 1, &g_contexts[index].context, nullptr) != ASTCENC_SUCCESS)
                return nullptr;
            return g_contexts[index].context;
        }

        uint32_t BlockBytes(uint32_t format)
        {
            switch (format)
            {
            case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM: return 8;
            case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM:
            case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM:
            case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM: return 16;
            case SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM: return 8;
            default: return 0;
            }
        }

        bool BCFormat(uint32_t format)
        {
            return format == SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM ||
                format == SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM ||
                format == SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM ||
                format == SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM ||
                format == SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
        }
    }

    SDL_GPUTextureFormat AstcFormat(int block)
    {
        if (block == 4)
            return SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;
        if (block == 6)
            return SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM;
        return SDL_GPU_TEXTUREFORMAT_INVALID;
    }

    int AstcBlockSize(SDL_GPUTextureFormat format)
    {
        switch (format)
        {
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM: return 4;
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM: return 6;
        default: return 0;
        }
    }

    bool AstcSupported(SDL_GPUDevice* device, SDL_GPUTextureType type, int block)
    {
        SDL_GPUTextureFormat format = AstcFormat(block);
        return device && format != SDL_GPU_TEXTUREFORMAT_INVALID &&
            SDL_GPUTextureSupportsFormat(device, format, type, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    }

    bool CanEncodeAstc(const Decoded& decoded)
    {
        return BCFormat(decoded.format);
    }

    bool EncodeAstc(const Decoded& decoded, int block, Decoded& out)
    {
        if (!CanEncodeAstc(decoded) || (block != 4 && block != 6) || decoded.width == 0 || decoded.height == 0)
            return false;

        uint32_t blockBytes = BlockBytes(decoded.format);
        uint32_t blocksX = (decoded.width + 3) / 4;
        uint32_t blocksY = (decoded.height + 3) / 4;
        if (decoded.data.size() < size_t(blocksX) * blocksY * blockBytes)
            return false;

        // Decode the BC blocks to tightly packed RGBA8. Edge blocks decode
        // whole but only the real width/height is passed to the encoder.
        std::vector<uint8_t> rgba(size_t(decoded.width) * decoded.height * 4);
        std::vector<uint8_t> blockPixels(4 * 4 * 4);
        for (uint32_t by = 0; by < blocksY; by++)
        {
            for (uint32_t bx = 0; bx < blocksX; bx++)
            {
                const uint8_t* source = decoded.data.data() + (size_t(by) * blocksX + bx) * blockBytes;
                switch (decoded.format)
                {
                case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM: bcdec_bc1(source, blockPixels.data(), 16); break;
                case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM: bcdec_bc2(source, blockPixels.data(), 16); break;
                case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM: bcdec_bc3(source, blockPixels.data(), 16); break;
                case SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM:
                {
                    uint8_t r[16];
                    bcdec_bc4(source, r, 4);
                    for (int i = 0; i < 16; i++)
                        blockPixels[i * 4] = blockPixels[i * 4 + 1] = blockPixels[i * 4 + 2] = r[i],
                        blockPixels[i * 4 + 3] = 255;
                    break;
                }
                case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:
                {
                    uint8_t rg[32];
                    bcdec_bc5(source, rg, 8);
                    for (int i = 0; i < 16; i++)
                    {
                        blockPixels[i * 4] = rg[i * 2];
                        blockPixels[i * 4 + 1] = rg[i * 2 + 1];
                        blockPixels[i * 4 + 2] = 0;
                        blockPixels[i * 4 + 3] = 255;
                    }
                    break;
                }
                default:
                    return false;
                }

                for (uint32_t y = 0; y < 4; y++)
                {
                    uint32_t dstY = by * 4 + y;
                    if (dstY >= decoded.height)
                        break;
                    uint32_t copy = std::min<uint32_t>(4, decoded.width - bx * 4);
                    memcpy(rgba.data() + (size_t(dstY) * decoded.width + bx * 4) * 4, blockPixels.data() + y * 16, copy * 4);
                }
            }
        }

        std::vector<void*> rows(decoded.height);
        for (uint32_t y = 0; y < decoded.height; y++)
            rows[y] = rgba.data() + size_t(y) * decoded.width * 4;

        astcenc_image image{};
        image.dim_x = decoded.width;
        image.dim_y = decoded.height;
        image.dim_z = 1;
        image.data_type = ASTCENC_TYPE_U8;
        image.data = rows.data();

        astcenc_swizzle swizzle{ ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
        uint32_t astcBlocksX = (decoded.width + block - 1) / block;
        uint32_t astcBlocksY = (decoded.height + block - 1) / block;
        size_t outputSize = size_t(astcBlocksX) * astcBlocksY * 16;
        out.data.resize(outputSize);

        std::lock_guard lock(g_mutex);
        astcenc_context* context = GetContext(block);
        if (!context)
            return false;
        if (astcenc_compress_image(context, &image, &swizzle, out.data.data(), out.data.size(), 0) != ASTCENC_SUCCESS)
            return false;
        astcenc_compress_reset(context);

        out.format = AstcFormat(block);
        out.width = decoded.width;
        out.height = decoded.height;
        out.rowTexels = astcBlocksX * block;
        out.rows = astcBlocksY * block;
        return true;
    }
}

#else

namespace h3::gpu::texture
{
    SDL_GPUTextureFormat AstcFormat(int) { return SDL_GPU_TEXTUREFORMAT_INVALID; }
    int AstcBlockSize(SDL_GPUTextureFormat) { return 0; }
    bool AstcSupported(SDL_GPUDevice*, SDL_GPUTextureType, int) { return false; }
    bool CanEncodeAstc(const Decoded&) { return false; }
    bool EncodeAstc(const Decoded&, int, Decoded&) { return false; }
}

#endif
