#include "perf/overlay.h"

#include "config/env.h"
#include "config/settings.h"
#include "gpu/renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <time.h>
#include <unistd.h>

namespace h3::perf
{
    namespace
    {
        struct Metrics
        {
            double fps = 0;
            double frameMs = 0;
            double cpuPercent = 0;
            int gpuPercent = -1;
            int gpuClockMHz = 0;
            double processRssMb = 0;
            double totalRamMb = 0;
            uint32_t frameDraws = 0;
            uint32_t frameFlushes = 0;
            uint32_t pipelines = 0;
            uint32_t frameWaitMs = 0;
            uint32_t targetWidth = 0, targetHeight = 0;
        };

        bool ReadTextFile(const char* path, std::string& text)
        {
            std::ifstream file(path);
            if (!file)
                return false;
            std::getline(file, text);
            return !text.empty();
        }

        int ReadIntFile(const char* path)
        {
            std::string text;
            if (!ReadTextFile(path, text))
                return -1;
            return atoi(text.c_str());
        }

        double ProcessRssMb()
        {
            std::ifstream file("/proc/self/statm");
            unsigned long totalPages = 0, residentPages = 0;
            if (!(file >> totalPages >> residentPages))
                return 0;
            long pageSize = sysconf(_SC_PAGESIZE);
            return pageSize > 0 ? double(residentPages) * double(pageSize) / (1024.0 * 1024.0) : 0;
        }

        double TotalRamMb()
        {
            std::ifstream file("/proc/meminfo");
            std::string key;
            unsigned long value = 0;
            std::string unit;
            while (file >> key >> value >> unit)
            {
                if (key == "MemTotal:")
                    return double(value) / 1024.0;
            }
            return 0;
        }

        double ProcessCpuPercent()
        {
            static bool haveLast = false;
            static timespec lastWall{}, lastCpu{};
            timespec wall{}, cpu{};
            clock_gettime(CLOCK_MONOTONIC, &wall);
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu);
            double result = 0;
            if (haveLast)
            {
                double wallSeconds = double(wall.tv_sec - lastWall.tv_sec) + double(wall.tv_nsec - lastWall.tv_nsec) * 1e-9;
                double cpuSeconds = double(cpu.tv_sec - lastCpu.tv_sec) + double(cpu.tv_nsec - lastCpu.tv_nsec) * 1e-9;
                if (wallSeconds > 0)
                    result = cpuSeconds / wallSeconds * 100.0;
            }
            lastWall = wall;
            lastCpu = cpu;
            haveLast = true;
            return std::clamp(result, 0.0, 9999.0);
        }

        void Update(Metrics& metrics, uint32_t presentedFrames, double elapsed)
        {
            // Frames actually presented by the guest; the window may redraw
            // faster than the game, so don't use the renderer's loop count.
            static uint32_t lastFrames = 0;
            static double lastTime = 0;
            static double smoothedFps = 0;

            double now = double(SDL_GetTicks()) / 1000.0;
            if (lastTime == 0)
            {
                lastFrames = presentedFrames;
                lastTime = now;
            }
            if (now - lastTime >= 0.35)
            {
                double instant = double(presentedFrames - lastFrames) / std::max(now - lastTime, 1e-6);
                smoothedFps = smoothedFps > 0 ? smoothedFps * 0.55 + instant * 0.45 : instant;
                lastFrames = presentedFrames;
                lastTime = now;
                metrics.fps = smoothedFps;
                metrics.frameMs = smoothedFps > 0.01 ? 1000.0 / smoothedFps : 0;
            }

            if (elapsed < 0.25)
                return;
            metrics.cpuPercent = ProcessCpuPercent();
            metrics.processRssMb = ProcessRssMb();
            metrics.totalRamMb = TotalRamMb();
            int busy = ReadIntFile("/sys/kernel/gpu/gpu_busy");
            if (busy < 0)
                busy = ReadIntFile("/sys/class/devfreq/10e00000.sgpu/gpu_load");
            metrics.gpuPercent = busy;
            int clockKhz = ReadIntFile("/sys/kernel/gpu/gpu_clock");
            if (clockKhz < 0)
                clockKhz = ReadIntFile("/sys/class/devfreq/10e00000.sgpu/cur_freq");
            metrics.gpuClockMHz = clockKhz > 0 ? clockKhz / 1000 : 0;
            gpu::renderer::PerformanceSnapshot snapshot = gpu::renderer::GetPerformance();
            metrics.frameDraws = snapshot.frameDraws;
            metrics.frameFlushes = snapshot.frameFlushes;
            metrics.pipelines = snapshot.pipelines;
            metrics.frameWaitMs = snapshot.frameWaitMs;
            metrics.targetWidth = snapshot.targetWidth;
            metrics.targetHeight = snapshot.targetHeight;
        }
    }

    void Draw(SDL_Renderer* renderer, float width, float height, uint32_t presentedFrames)
    {
        const config::VideoSettings& video = config::Active().video;
        bool enabled = video.performanceOverlay;
        if (const char* env = std::getenv("H3_PERF_OVERLAY"); env && *env)
            enabled = EnvFlag("H3_PERF_OVERLAY");
        if (!enabled || !renderer || width <= 0 || height <= 0)
            return;

        static Metrics metrics;
        static double lastUpdate = 0;
        double now = double(SDL_GetTicks()) / 1000.0;
        Update(metrics, presentedFrames, now - lastUpdate);
        if (now - lastUpdate >= 0.25)
            lastUpdate = now;

        const char* gpu = metrics.gpuPercent >= 0 ? nullptr : "GPU --";
        char lineGpu[64];
        if (gpu)
            snprintf(lineGpu, sizeof(lineGpu), "%s", gpu);
        else if (metrics.gpuClockMHz > 0)
            snprintf(lineGpu, sizeof(lineGpu), "GPU %d%%  %d MHz", metrics.gpuPercent, metrics.gpuClockMHz);
        else
            snprintf(lineGpu, sizeof(lineGpu), "GPU %d%%", metrics.gpuPercent);

        char lineFps[80], lineCpu[64], lineRam[64], lineWait[80], linePass[64];
        if (metrics.targetWidth > 0)
            snprintf(lineFps, sizeof(lineFps), "FPS %.1f  %.0f ms  %ux%u", metrics.fps, metrics.frameMs,
                metrics.targetWidth, metrics.targetHeight);
        else
            snprintf(lineFps, sizeof(lineFps), "FPS %.1f  %.0f ms", metrics.fps, metrics.frameMs);
        snprintf(lineCpu, sizeof(lineCpu), "CPU %.0f%%", metrics.cpuPercent);
        snprintf(lineRam, sizeof(lineRam), "RAM %.2f / %.2f GB", metrics.processRssMb / 1024.0, metrics.totalRamMb / 1024.0);
        snprintf(lineWait, sizeof(lineWait), "WAIT %u ms  DRAW %u", metrics.frameWaitMs, metrics.frameDraws);
        snprintf(linePass, sizeof(linePass), "PASS %u  PIPE %u", metrics.frameFlushes, metrics.pipelines);

        const float scale = 2.0f;
        const float lineHeight = 8.0f * scale + 2.0f;
        const float panelWidth = std::min(width - 24.0f, 390.0f);
        const float panelHeight = lineHeight * 6 + 12.0f;
        const float x = 12.0f;
        const float y = std::min(height - panelHeight - 12.0f, 108.0f);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 4, 12, 7, 190);
        SDL_FRect panel{ x, y, panelWidth, panelHeight };
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 120, 220, 135, 210);
        SDL_RenderRect(renderer, &panel);

        SDL_SetRenderScale(renderer, scale, scale);
        SDL_SetRenderDrawColor(renderer, 210, 255, 218, 235);
        const char* lines[] = { lineFps, lineCpu, lineGpu, lineRam, lineWait, linePass };
        for (int i = 0; i < 6; i++)
            SDL_RenderDebugText(renderer, (x + 8.0f) / scale, (y + 6.0f + lineHeight * i) / scale, lines[i]);
        SDL_SetRenderScale(renderer, 1, 1);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
}
