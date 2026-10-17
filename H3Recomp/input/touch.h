// On-screen Xbox 360 pad (Android, and any host with a touch device).
// The overlay is drawn over the finished game frame; touches are merged
// into XInput state by input.cpp.

#pragma once

#include <cstdint>

union SDL_Event;
struct SDL_Renderer;

namespace h3::input::touch
{
    struct State
    {
        uint16_t buttons = 0;
        uint8_t leftTrigger = 0;
        uint8_t rightTrigger = 0;
        int16_t leftX = 0, leftY = 0;
        int16_t rightX = 0, rightY = 0;
    };

    bool Enabled();
    bool HasTouchDevice();

    // Handles SDL_EVENT_FINGER_*; returns true when the event was part of
    // the overlay (so it doesn't fall through to other input).
    bool HandleEvent(const SDL_Event& event);

    State GetState();

    // Draws the pad. width/height are the renderer's output size.
    void Draw(SDL_Renderer* renderer, float width, float height);
}
