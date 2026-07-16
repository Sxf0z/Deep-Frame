#pragma once
#include "pch.h"
#include <cstdarg>

namespace df {

inline void Log(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

inline void ThrowIfFailed(HRESULT hr, const char* what)
{
    if (FAILED(hr))
    {
        Log("[DeepFrame] FATAL %s hr=0x%08X", what, static_cast<unsigned>(hr));
        winrt::throw_hresult(hr);
    }
}

struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
IDirect3DDxgiInterfaceAccess : ::IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** p) = 0;
};

struct WindowInfo
{
    HWND         hwnd = nullptr;
    std::wstring title;
    std::wstring className;
};

struct GpuInfo
{
    int          index = 0;          // DXGI adapter ordinal
    std::wstring name;               // e.g. "NVIDIA GeForce RTX 3050 Ti Laptop GPU"
    UINT         vendorId = 0;
    UINT         deviceId = 0;
    size_t       dedicatedBytes = 0;
    bool         isNvidia = false;
    bool         isIntel = false;
    bool         isAmd = false;
};

// Accurate FPS: count events inside a 1.0s sliding window (LS-style).
struct SlidingFps
{
    static constexpr int kMax = 512;
    LONGLONG stamps[kMax]{};
    int head = 0;
    int count = 0;
    LONGLONG freq = 0;

    void Init()
    {
        QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&freq));
        if (freq <= 0) freq = 1;
        head = count = 0;
    }

    void Hit()
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        stamps[head] = now.QuadPart;
        head = (head + 1) % kMax;
        if (count < kMax) ++count;
    }

    float Rate() const
    {
        if (count < 2 || freq <= 0) return 0.f;

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const LONGLONG window = freq; // 1 second
        const LONGLONG cutoff = now.QuadPart - window;

        int n = 0;
        LONGLONG oldest = 0;
        bool haveOldest = false;
        for (int i = 0; i < count; ++i)
        {
            int idx = (head - 1 - i + kMax * 2) % kMax;
            LONGLONG t = stamps[idx];
            if (t < cutoff) break;
            ++n;
            oldest = t;
            haveOldest = true;
        }
        if (n < 2 || !haveOldest) return float(n); // cold start
        double sec = double(now.QuadPart - oldest) / double(freq);
        if (sec < 0.05) return float(n) / 0.05f;
        // n events over `sec` → approximate rate (n-1 intervals)
        return float((n - 1) / sec);
    }
};

struct RuntimeState
{
    std::atomic<bool> running{false};
    std::atomic<bool> quit{false};
    std::atomic<bool> frameGen{true};
    std::atomic<bool> upscale{false};
    std::atomic<bool> performanceMode{true};
    std::atomic<int>  multiplier{2};
    std::atomic<int>  flowScalePct{50}; // balanced sharpness / cost
    std::atomic<int>  gpuAdapterIndex{0}; // DXGI adapter index (prefer RTX)

    // HUD modes
    std::atomic<int>  countdown{-1}; // -1 = off, 0..5 = seconds left
    std::atomic<bool> hudVisible{false};

    std::mutex        targetMutex;
    HWND              targetHwnd = nullptr;
    std::wstring      targetTitle;

    std::atomic<uint64_t> framesCaptured{0};
    std::atomic<uint64_t> framesPresented{0};
    std::atomic<uint64_t> genPresented{0};
    std::atomic<float>    avgIntervalMs{16.67f};

    std::atomic<float> sourceFps{0.f};
    std::atomic<float> outputFps{0.f};

    char     status[256]{};
    std::mutex statusMutex;

    void SetStatus(const char* s)
    {
        std::lock_guard lock(statusMutex);
        snprintf(status, sizeof(status), "%s", s);
    }
};

// Hotkey IDs
constexpr int kHotkeyF6 = 10;
constexpr int kHotkeyFgToggle = 11;

// App messages
constexpr UINT WM_DF_REFRESH   = WM_APP + 1;
constexpr UINT WM_DF_SCALE     = WM_APP + 2;
constexpr UINT WM_DF_STOP      = WM_APP + 3;
constexpr UINT WM_DF_THREADEND = WM_APP + 4;
constexpr UINT WM_DF_COUNTDOWN = WM_APP + 5;
constexpr UINT WM_DF_TOGGLE    = WM_APP + 6;

} // namespace df
