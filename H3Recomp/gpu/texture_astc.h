// ASTC conversion for textures the host would otherwise upload as BC.
// Mali (and most mobile GPUs) sample ASTC natively; BC is often missing.
// The CPU encoder uses Arm's astcenc with NEON, and the result is cached on
// disk so a texture is converted once.

#pragma once

#include "gpu/texture.h"

struct SDL_GPUDevice;

namespace h3::gpu::texture
{
    // The SDL format for a square ASTC block size (4 or 6), or INVALID.
    SDL_GPUTextureFormat AstcFormat(int block);
    // 4 or 6 for an ASTC format, 0 otherwise.
    int AstcBlockSize(SDL_GPUTextureFormat format);

    // Whether the device can sample this ASTC format as the given texture
    // type (2D, cube, 2D array).
    bool AstcSupported(SDL_GPUDevice* device, SDL_GPUTextureType type, int block);

    // Whether a decoded texture is a BC format worth converting (BC1-BC5).
    bool CanEncodeAstc(const Decoded& decoded);

    // Decodes BC data and encodes it as ASTC. out has the ASTC format and
    // block-padded rowTexels/rows.
    bool EncodeAstc(const Decoded& decoded, int block, Decoded& out);
}
