// In-game performance overlay: frame rate, process CPU, GPU busy and RAM.
// Drawn with the SDL renderer after the game frame, so it works on every
// Android GPU without touching the guest renderer.

#pragma once

#include <cstdint>

struct SDL_Renderer;

namespace h3::perf
{
    // Draws the overlay when config::Active().video.performanceOverlay is on.
    // presentedFrames is the command processor's VdSwap counter.
    void Draw(SDL_Renderer* renderer, float width, float height, uint32_t presentedFrames);
}
