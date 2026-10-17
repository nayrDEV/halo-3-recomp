// On-screen Xbox 360 pad. Layout and hit-testing share one table so a drawn
// button is always the button a finger lands on.

#include "input/touch.h"
#include "input/input.h"
#include "config/settings.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace h3::input::touch
{
    namespace
    {
        enum class Role : uint8_t
        {
            None, LeftStick, RightStick,
            A, B, X, Y, LB, RB, LT, RT, Start, Back,
            DpadUp, DpadDown, DpadLeft, DpadRight,
            Count
        };

        struct Button
        {
            Role role;
            const char* label;
            float cx, cy;     // centre, fractions of the output size
            float size;       // fraction of height (square)
            SDL_Color color;
        };

        // A 16:9-friendly mobile layout: movement bottom-left, look
        // bottom-right, face buttons above the right stick, shoulders and
        // triggers at the top corners, D-pad left of the face buttons.
        constexpr Button kButtons[] = {
            { Role::A, "A", 0.89f, 0.62f, 0.075f, { 100, 200, 70, 190 } },
            { Role::B, "B", 0.955f, 0.54f, 0.075f, { 210, 80, 70, 190 } },
            { Role::X, "X", 0.825f, 0.54f, 0.075f, { 70, 130, 230, 190 } },
            { Role::Y, "Y", 0.89f, 0.46f, 0.075f, { 220, 190, 60, 190 } },
            { Role::LB, "LB", 0.085f, 0.07f, 0.075f, { 200, 200, 200, 120 } },
            { Role::RB, "RB", 0.845f, 0.07f, 0.075f, { 200, 200, 200, 120 } },
            { Role::LT, "LT", 0.035f, 0.22f, 0.085f, { 180, 180, 180, 110 } },
            { Role::RT, "RT", 0.965f, 0.22f, 0.085f, { 180, 180, 180, 110 } },
            { Role::Start, "START", 0.50f, 0.035f, 0.070f, { 220, 220, 220, 120 } },
            { Role::Back, "BACK", 0.42f, 0.035f, 0.070f, { 220, 220, 220, 120 } },
            { Role::DpadUp, "^", 0.20f, 0.31f, 0.065f, { 190, 200, 190, 130 } },
            { Role::DpadDown, "v", 0.20f, 0.45f, 0.065f, { 190, 200, 190, 130 } },
            { Role::DpadLeft, "<", 0.13f, 0.38f, 0.065f, { 190, 200, 190, 130 } },
            { Role::DpadRight, ">", 0.27f, 0.38f, 0.065f, { 190, 200, 190, 130 } },
        };

        constexpr float kStickCX = 0.14f, kStickCY = 0.74f;
        constexpr float kRightStickCX = 0.74f, kRightStickCY = 0.74f;
        constexpr float kStickRadius = 0.11f;  // fraction of height

        struct Touch
        {
            Role role = Role::None;
            float x = 0, y = 0;
        };

        std::unordered_map<SDL_FingerID, Touch> g_touches;

        SDL_FRect ButtonRect(const Button& button, float width, float height)
        {
            float side = button.size * height;
            return SDL_FRect{ button.cx * width - side / 2, button.cy * height - side / 2, side, side };
        }

        bool InStick(float x, float y, float cx, float cy, float width, float height)
        {
            float dx = (x - cx) * width, dy = (y - cy) * height;
            float radius = kStickRadius * height;
            return dx * dx + dy * dy <= radius * radius * 1.8f;
        }

        bool HitButton(float x, float y, float width, float height, Role& role)
        {
            for (const Button& button : kButtons)
            {
                SDL_FRect rect = ButtonRect(button, width, height);
                if (x * width >= rect.x && x * width < rect.x + rect.w && y * height >= rect.y && y * height < rect.y + rect.h)
                {
                    role = button.role;
                    return true;
                }
            }
            return false;
        }

        void DrawButton(SDL_Renderer* renderer, const Button& button, bool pressed, float width, float height)
        {
            SDL_FRect rect = ButtonRect(button, width, height);
            SDL_Color color = button.color;
            color.a = pressed ? 230 : color.a;
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(renderer, &rect);
            SDL_SetRenderDrawColor(renderer, 240, 245, 235, pressed ? 230 : 120);
            SDL_RenderRect(renderer, &rect);

            // Debug text is 8px; scale it up for a phone-sized overlay.
            const float scale = 2.0f;
            size_t length = std::strlen(button.label);
            float textWidth = float(length) * 8.0f;
            SDL_SetRenderScale(renderer, scale, scale);
            SDL_SetRenderDrawColor(renderer, 250, 252, 245, 235);
            SDL_RenderDebugText(renderer, (rect.x + (rect.w - textWidth * scale) / 2) / scale,
                (rect.y + (rect.h - 8 * scale) / 2) / scale, button.label);
            SDL_SetRenderScale(renderer, 1, 1);
        }

        void DrawStick(SDL_Renderer* renderer, float cx, float cy, float x, float y, float width, float height)
        {
            float radius = kStickRadius * height;
            SDL_FRect base{ cx * width - radius, cy * height - radius, radius * 2, radius * 2 };
            SDL_SetRenderDrawColor(renderer, 150, 170, 150, 45);
            SDL_RenderFillRect(renderer, &base);
            SDL_SetRenderDrawColor(renderer, 190, 210, 185, 100);
            SDL_RenderRect(renderer, &base);

            float dx = std::clamp((x - cx) * width / radius, -1.0f, 1.0f);
            float dy = std::clamp((y - cy) * height / radius, -1.0f, 1.0f);
            float knob = radius * 0.42f;
            SDL_FRect inner{ cx * width + dx * radius * 0.55f - knob, cy * height + dy * radius * 0.55f - knob,
                knob * 2, knob * 2 };
            SDL_SetRenderDrawColor(renderer, 205, 230, 190, 150);
            SDL_RenderFillRect(renderer, &inner);
            SDL_SetRenderDrawColor(renderer, 240, 250, 235, 200);
            SDL_RenderRect(renderer, &inner);
        }

        uint16_t ButtonBit(Role role)
        {
            switch (role)
            {
            case Role::A: return kA;
            case Role::B: return kB;
            case Role::X: return kX;
            case Role::Y: return kY;
            case Role::LB: return kLeftShoulder;
            case Role::RB: return kRightShoulder;
            case Role::Start: return kStart;
            case Role::Back: return kBack;
            case Role::DpadUp: return kDpadUp;
            case Role::DpadDown: return kDpadDown;
            case Role::DpadLeft: return kDpadLeft;
            case Role::DpadRight: return kDpadRight;
            default: return 0;
            }
        }
    }

    bool Enabled()
    {
        return config::Active().video.touchControls;
    }

    bool HasTouchDevice()
    {
#if defined(__ANDROID__)
        static const bool has = [] {
            int count = 0;
            SDL_TouchID* devices = SDL_GetTouchDevices(&count);
            SDL_free(devices);
            return count > 0;
        }();
        return has;
#else
        return false;
#endif
    }

    bool HandleEvent(const SDL_Event& event)
    {
        if (!Enabled())
            return false;

        switch (event.type)
        {
        case SDL_EVENT_FINGER_DOWN:
        {
            Role role = Role::None;
            // The overlay layout is relative to the window; the event's
            // x/y are already normalized to it.
            SDL_Window* window = SDL_GetWindowFromID(event.tfinger.windowID);
            int w = 1, h = 1;
            if (window)
                SDL_GetWindowSizeInPixels(window, &w, &h);
            if (HitButton(event.tfinger.x, event.tfinger.y, float(w), float(h), role) ||
                InStick(event.tfinger.x, event.tfinger.y, kStickCX, kStickCY, float(w), float(h)))
                role = role == Role::None ? Role::LeftStick : role;
            else if (InStick(event.tfinger.x, event.tfinger.y, kRightStickCX, kRightStickCY, float(w), float(h)))
                role = Role::RightStick;
            if (role == Role::None)
                return false;
            g_touches[event.tfinger.fingerID] = { role, event.tfinger.x, event.tfinger.y };
            return true;
        }
        case SDL_EVENT_FINGER_MOTION:
        {
            auto found = g_touches.find(event.tfinger.fingerID);
            if (found == g_touches.end())
                return false;
            found->second.x = event.tfinger.x;
            found->second.y = event.tfinger.y;
            return true;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            return g_touches.erase(event.tfinger.fingerID) != 0;
        default:
            return false;
        }
    }

    State GetState()
    {
        State state;
        for (const auto& [id, touch] : g_touches)
        {
            if (uint16_t bit = ButtonBit(touch.role))
                state.buttons |= bit;
            if (touch.role == Role::LT)
                state.leftTrigger = kTriggerMax;
            else if (touch.role == Role::RT)
                state.rightTrigger = kTriggerMax;
            else if (touch.role == Role::LeftStick)
            {
                state.leftX = int16_t(std::clamp((touch.x - kStickCX) / kStickRadius, -1.0f, 1.0f) * kStickMax);
                state.leftY = int16_t(std::clamp(-(touch.y - kStickCY) / kStickRadius, -1.0f, 1.0f) * kStickMax);
            }
            else if (touch.role == Role::RightStick)
            {
                state.rightX = int16_t(std::clamp((touch.x - kRightStickCX) / kStickRadius, -1.0f, 1.0f) * kStickMax);
                state.rightY = int16_t(std::clamp(-(touch.y - kRightStickCY) / kStickRadius, -1.0f, 1.0f) * kStickMax);
            }
        }
        return state;
    }

    void Draw(SDL_Renderer* renderer, float width, float height)
    {
        if (!renderer || width <= 0 || height <= 0)
            return;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        State state = GetState();
        for (const Button& button : kButtons)
        {
            bool pressed = (ButtonBit(button.role) & state.buttons) != 0 ||
                ((button.role == Role::LT || button.role == Role::RT) &&
                    (button.role == Role::LT ? state.leftTrigger : state.rightTrigger) != 0);
            DrawButton(renderer, button, pressed, width, height);
        }

        float leftX = kStickCX, leftY = kStickCY, rightX = kRightStickCX, rightY = kRightStickCY;
        for (const auto& [id, touch] : g_touches)
        {
            if (touch.role == Role::LeftStick)
                leftX = touch.x, leftY = touch.y;
            else if (touch.role == Role::RightStick)
                rightX = touch.x, rightY = touch.y;
        }
        DrawStick(renderer, kStickCX, kStickCY, leftX, leftY, width, height);
        DrawStick(renderer, kRightStickCX, kRightStickCY, rightX, rightY, width, height);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
}
