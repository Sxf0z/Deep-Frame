#include "engine.h"

namespace df {
namespace {

constexpr UINT_PTR kTimerCountdown = 42;

double QpcMs(const LARGE_INTEGER& a, const LARGE_INTEGER& b, LONGLONG freq)
{
    return double(a.QuadPart - b.QuadPart) * 1000.0 / double(freq);
}

bool WaitMs(HANDLE hTimer, HANDLE stopEvent, double ms)
{
    if (ms <= 0.05) return WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0;
    ms = std::clamp(ms, 0.05, 50.0);
    if (hTimer)
    {
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(ms * 10000.0);
        SetWaitableTimerEx(hTimer, &due, 0, nullptr, nullptr, nullptr, 0);
        HANDLE w[] = { hTimer, stopEvent };
        DWORD wr = WaitForMultipleObjects(2, w, FALSE, (DWORD)(ms + 5.0));
        return wr != WAIT_OBJECT_0 + 1;
    }
    return WaitForSingleObject(stopEvent, (DWORD)ms) != WAIT_OBJECT_0;
}

} // namespace

bool Engine::Init(HINSTANCE inst)
{
    m_inst = inst;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    auto gpus = EnumerateGpus();
    int preferred = FindPreferredGpuIndex(gpus);
    m_state.gpuAdapterIndex.store(preferred);

    if (!m_gfx.Init(preferred))
    {
        MessageBoxW(nullptr, L"Failed to create D3D11 device.\nPick another GPU in the list.", L"DeepFrame", MB_ICONERROR);
        return false;
    }

    std::wstring base = Graphics::ExeDirectory();
    if (!m_gfx.LoadShaders(base) &&
        !m_gfx.LoadShaders(base + L"..\\..\\src\\") &&
        !m_gfx.LoadShaders(L"src\\"))
    {
        MessageBoxW(nullptr, L"Failed to load compute shaders.\nRebuild Release.", L"DeepFrame", MB_ICONERROR);
        return false;
    }

    m_capture = std::make_unique<CaptureSession>(m_gfx);
    m_fg = std::make_unique<FrameGenPipeline>(m_gfx);

    if (!m_overlay.Create(inst) || !m_panel.Create(inst, m_state) || !m_fpsHud.Create(inst, m_state))
    {
        MessageBoxW(nullptr, L"Failed to create UI.", L"DeepFrame", MB_ICONERROR);
        return false;
    }

    m_panel.SetGpuList(gpus, m_gfx.ActiveAdapterIndex());

    {
        char name[256]{};
        WideCharToMultiByte(CP_UTF8, 0, m_gfx.ActiveAdapterName().c_str(), -1, name, 256, nullptr, nullptr);
        char buf[320];
        snprintf(buf, sizeof(buf), "Ready on %s — F6 to Scale", name);
        m_state.SetStatus(buf);
    }

    RefreshWindowList();
    RegisterHotkeys();
    return true;
}

void Engine::RegisterHotkeys()
{
    HWND h = m_panel.Hwnd();
    RegisterHotKey(h, kHotkeyF6, 0, VK_F6);
    RegisterHotKey(h, kHotkeyFgToggle, MOD_CONTROL | MOD_SHIFT, 'F');
}

void Engine::UnregisterHotkeys()
{
    HWND h = m_panel.Hwnd();
    if (!IsWindow(h)) return;
    UnregisterHotKey(h, kHotkeyF6);
    UnregisterHotKey(h, kHotkeyFgToggle);
}

void Engine::Shutdown()
{
    KillTimer(m_panel.Hwnd(), kTimerCountdown);
    StopCapture();
    UnregisterHotkeys();
    m_fpsHud.Destroy();
    m_panel.Destroy();
    m_overlay.Destroy();
    m_fg.reset();
    m_capture.reset();
    m_gfx.Shutdown();
    if (m_stopEvent) { CloseHandle(m_stopEvent); m_stopEvent = nullptr; }
}

void Engine::RefreshWindowList()
{
    m_panel.SetWindowList(CaptureSession::EnumerateWindows());
}

void Engine::BeginScaleSequence()
{
    if (m_state.running.load() || m_threadAlive.load() || m_countdownActive)
        return;

    m_panel.ReadControlsToState();

    const int wantGpu = m_state.gpuAdapterIndex.load();
    if (wantGpu != m_gfx.ActiveAdapterIndex())
    {
        m_state.SetStatus("Switching GPU…");
        m_panel.SyncFromState();
        m_fg.reset();
        m_capture.reset();
        if (!m_gfx.RecreateDevice(wantGpu))
        {
            m_state.SetStatus("GPU switch failed");
            m_panel.SyncFromState();
            return;
        }
        std::wstring base = Graphics::ExeDirectory();
        if (!m_gfx.LoadShaders(base) &&
            !m_gfx.LoadShaders(base + L"..\\..\\src\\") &&
            !m_gfx.LoadShaders(L"src\\"))
        {
            m_state.SetStatus("Shader reload failed");
            m_panel.SyncFromState();
            return;
        }
        m_capture = std::make_unique<CaptureSession>(m_gfx);
        m_fg = std::make_unique<FrameGenPipeline>(m_gfx);
    }

    m_countdownActive = true;
    m_panel.ShowPanel(false);
    m_state.countdown.store(5);
    m_fpsHud.Show(true);
    m_fpsHud.ForceRedraw();
    m_state.SetStatus("Click your game… 5");
    SetTimer(m_panel.Hwnd(), kTimerCountdown, 1000, nullptr);
}

void Engine::OnCountdownTick()
{
    if (!m_countdownActive) return;
    int cd = m_state.countdown.load();
    if (cd > 1)
    {
        m_state.countdown.store(cd - 1);
        char buf[64];
        snprintf(buf, sizeof(buf), "Click your game… %d", cd - 1);
        m_state.SetStatus(buf);
        m_fpsHud.ForceRedraw();
        return;
    }

    KillTimer(m_panel.Hwnd(), kTimerCountdown);
    m_countdownActive = false;
    m_state.countdown.store(0);
    m_fpsHud.ForceRedraw();

    HWND target = ResolveForegroundTarget();
    if (!target) target = m_panel.SelectedTarget();
    Sleep(80);
    m_state.countdown.store(-1);

    if (!target || !IsWindow(target) || IsDeepFrameWindow(target))
    {
        m_fpsHud.Show(false);
        m_panel.ShowPanel(true);
        m_state.SetStatus("No game window — try again");
        m_panel.SyncFromState();
        return;
    }
    StartCaptureOnTarget(target);
}

void Engine::StartCaptureOnTarget(HWND target)
{
    if (m_state.running.load() || m_threadAlive.load()) return;
    m_panel.ReadControlsToState();

    {
        std::lock_guard lock(m_state.targetMutex);
        m_state.targetHwnd = target;
        wchar_t title[256]{};
        GetWindowTextW(target, title, 256);
        m_state.targetTitle = title;
    }

    if (!m_capture->Start(target))
    {
        m_fpsHud.Show(false);
        m_panel.ShowPanel(true);
        m_state.SetStatus("Capture failed — use borderless + same GPU as game");
        m_panel.SyncFromState();
        return;
    }

    float flowScale = m_state.flowScalePct.load() / 100.0f;
    if (!m_fg->EnsureSize(m_capture->Width(), m_capture->Height(), flowScale))
    {
        m_capture->Stop();
        m_fpsHud.Show(false);
        m_panel.ShowPanel(true);
        m_state.SetStatus("Out of VRAM");
        m_panel.SyncFromState();
        return;
    }

    m_overlay.ShowOver(target);
    if (!m_gfx.CreateOverlayTarget(m_overlay.Hwnd(), m_capture->Width(), m_capture->Height()))
    {
        m_capture->Stop();
        m_overlay.Hide();
        m_fpsHud.Show(false);
        m_panel.ShowPanel(true);
        m_state.SetStatus("Overlay failed");
        m_panel.SyncFromState();
        return;
    }

    ResetEvent(m_stopEvent);
    m_state.framesCaptured.store(0);
    m_state.framesPresented.store(0);
    m_state.genPresented.store(0);
    m_state.sourceFps.store(0.f);
    m_state.outputFps.store(0.f);
    m_state.countdown.store(-1);
    m_state.running.store(true);
    m_threadAlive.store(true);
    m_fpsHud.Show(true);
    m_state.SetStatus("Live — F6 to stop");
    m_renderThread = std::thread([this] { RenderThreadMain(); });
}

void Engine::TeardownCaptureResources()
{
    try { if (m_capture) m_capture->Stop(); } catch (...) {}
    try { m_gfx.DestroyOverlayTarget(); } catch (...) {}
    try { m_overlay.Hide(); } catch (...) {}
    try { if (m_fg) m_fg->Release(); } catch (...) {}
}

void Engine::StopCapture()
{
    m_countdownActive = false;
    KillTimer(m_panel.Hwnd(), kTimerCountdown);
    m_state.countdown.store(-1);
    m_state.running.store(false);
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_capture) SetEvent(m_capture->FrameEvent());
    if (m_renderThread.joinable()) m_renderThread.join();
    m_threadAlive.store(false);
    TeardownCaptureResources();
    m_fpsHud.Show(false);
    m_state.sourceFps.store(0.f);
    m_state.outputFps.store(0.f);
    m_state.SetStatus("Stopped");
    m_panel.ShowPanel(true);
    m_panel.SyncFromState();
    if (m_stopEvent) ResetEvent(m_stopEvent);
}

void Engine::ToggleScale()
{
    if (m_countdownActive)
    {
        KillTimer(m_panel.Hwnd(), kTimerCountdown);
        m_countdownActive = false;
        m_state.countdown.store(-1);
        m_fpsHud.Show(false);
        m_panel.ShowPanel(true);
        m_state.SetStatus("Cancelled");
        m_panel.SyncFromState();
        return;
    }
    if (m_state.running.load() || m_threadAlive.load())
        StopCapture();
    else
        BeginScaleSequence();
}

// ═══════════════════════════════════════════════════════════════════════════
// FINAL low-latency FG loop
//
//   capture → sharp FG → Present REAL now → wait half → Present GEN
//
// Real is never delayed for generated frames.
// Gen uses single-warp (no dual blend) → no motion blur soup.
// ═══════════════════════════════════════════════════════════════════════════
void Engine::RenderThreadMain()
{
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    catch (...) {}

    HANDLE hTimer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!hTimer)
        hTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    LARGE_INTEGER freqLI{};
    QueryPerformanceFrequency(&freqLI);
    const LONGLONG freq = freqLI.QuadPart;

    double avgIntervalMs = 16.667;
    constexpr double kAlpha = 0.22;
    LARGE_INTEGER lastCap{};
    bool haveLast = false;

    SlidingFps srcFps, outFps;
    srcFps.Init();
    outFps.Init();

    HWND target = nullptr;
    {
        std::lock_guard lock(m_state.targetMutex);
        target = m_state.targetHwnd;
    }

    Log("FINAL render path: sharp warp FG + real-first x2");

    try
    {
        while (m_state.running.load(std::memory_order_acquire) && !m_state.quit.load())
        {
            HANDLE waits[] = { m_capture->FrameEvent(), m_stopEvent };
            DWORD wr = WaitForMultipleObjects(2, waits, FALSE, 100);
            if (!m_state.running.load()) break;
            if (wr == WAIT_OBJECT_0 + 1) break;
            if (wr != WAIT_OBJECT_0) continue;

            if (!target || !IsWindow(target))
            {
                m_state.SetStatus("Target closed");
                break;
            }
            if (IsIconic(target)) continue;
            m_overlay.FollowTarget(target);

            LARGE_INTEGER capQpc{};
            QueryPerformanceCounter(&capQpc);
            if (haveLast)
            {
                double dt = QpcMs(capQpc, lastCap, freq);
                if (dt > 2.0 && dt < 100.0)
                    avgIntervalMs = kAlpha * dt + (1.0 - kAlpha) * avgIntervalMs;
            }
            lastCap = capQpc;
            haveLast = true;
            m_state.avgIntervalMs.store(float(avgIntervalMs));

            m_fg->BeginFrame();

            UINT cw = 0, ch = 0;
            if (!m_capture->TryAcquireLatest(m_fg->ColorCurr(), cw, ch))
                continue;

            srcFps.Hit();
            m_state.sourceFps.store(srcFps.Rate());
            ResetEvent(m_capture->FrameEvent());

            float flowScale = m_state.flowScalePct.load() / 100.0f;
            if (cw != m_fg->Width() || ch != m_fg->Height())
            {
                if (!m_fg->EnsureSize(cw, ch, flowScale))
                {
                    m_state.SetStatus("Resize failed");
                    break;
                }
                m_gfx.ResizeOverlay(cw, ch);
                if (!m_capture->TryAcquireLatest(m_fg->ColorCurr(), cw, ch))
                    continue;
                srcFps.Hit();
                m_state.sourceFps.store(srcFps.Rate());
                ResetEvent(m_capture->FrameEvent());
            }

            const bool doFg = m_state.frameGen.load();
            const bool doUp = m_state.upscale.load();
            const bool perf = m_state.performanceMode.load();
            const int mult = std::clamp(m_state.multiplier.load(), 2, 4);

            LARGE_INTEGER t0{};
            QueryPerformanceCounter(&t0);

            m_fg->Process(doFg, perf, mult);
            m_state.framesCaptured.fetch_add(1);

            LARGE_INTEGER t1{};
            QueryPerformanceCounter(&t1);
            const double gpuMs = QpcMs(t1, t0, freq);

            // 1) REAL — always first, always immediate
            if (!m_gfx.PresentSrv(m_fg->SrvCurr(), doUp, 0.55f))
            {
                m_state.SetStatus("Present failed — try RTX GPU");
                break;
            }
            m_state.framesPresented.fetch_add(1);
            outFps.Hit();
            m_state.outputFps.store(outFps.Rate());

            // 2) GENERATED — spaced at interval/mult after capture time
            const int gen = (doFg && m_fg->HasHistory()) ? m_fg->GeneratedCount() : 0;
            if (gen > 0)
            {
                for (int i = 0; i < gen; ++i)
                {
                    // Target: capture + (i+1)/mult * sourceInterval
                    double targetMs = avgIntervalMs * double(i + 1) / double(mult);
                    LARGE_INTEGER now{};
                    QueryPerformanceCounter(&now);
                    double elapsed = QpcMs(now, capQpc, freq);
                    double sleepMs = targetMs - elapsed;

                    // Keep a minimum gap so DWM sees a distinct present
                    if (sleepMs < 0.35) sleepMs = 0.35;
                    // Never sleep into the next capture window too hard
                    if (sleepMs > avgIntervalMs * 0.85)
                        sleepMs = avgIntervalMs * 0.45;

                    if (!WaitMs(hTimer, m_stopEvent, sleepMs))
                        goto done;

                    if (!m_gfx.PresentSrv(m_fg->SrvInterp(i), doUp, 0.55f))
                    {
                        m_state.SetStatus("Gen present failed");
                        goto done;
                    }
                    m_state.framesPresented.fetch_add(1);
                    m_state.genPresented.fetch_add(1);
                    outFps.Hit();
                    m_state.outputFps.store(outFps.Rate());
                }
            }

            if ((m_state.framesCaptured.load() % 30) == 1)
            {
                char buf[200];
                snprintf(buf, sizeof(buf),
                    "%.0f / %.0f fps · gpu %.1fms · x%d",
                    m_state.sourceFps.load(), m_state.outputFps.load(),
                    gpuMs, doFg ? mult : 1);
                m_state.SetStatus(buf);
            }
        }
    }
    catch (const winrt::hresult_error& e)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "Error 0x%08X", (unsigned)e.code());
        m_state.SetStatus(buf);
    }
    catch (...)
    {
        m_state.SetStatus("Render crash");
    }

done:
    if (hTimer) { CancelWaitableTimer(hTimer); CloseHandle(hTimer); }
    try
    {
        if (m_gfx.Context())
        {
            m_gfx.Context()->ClearState();
            m_gfx.Context()->Flush();
        }
    }
    catch (...) {}

    m_state.running.store(false);
    m_threadAlive.store(false);
    HWND panel = m_panel.Hwnd();
    if (panel && IsWindow(panel))
        PostMessageW(panel, WM_DF_THREADEND, 0, 0);
    try { winrt::uninit_apartment(); } catch (...) {}
}

int Engine::Run()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_HOTKEY)
        {
            if (msg.wParam == kHotkeyF6)
                PostMessageW(m_panel.Hwnd(), WM_DF_TOGGLE, 0, 0);
            else if (msg.wParam == kHotkeyFgToggle)
            {
                bool v = !m_state.frameGen.load();
                m_state.frameGen.store(v);
                m_state.SetStatus(v ? "FG ON" : "FG OFF");
            }
        }
        if (msg.message == WM_TIMER && msg.wParam == kTimerCountdown)
            OnCountdownTick();

        switch (msg.message)
        {
        case WM_DF_REFRESH: RefreshWindowList(); break;
        case WM_DF_SCALE:   BeginScaleSequence(); break;
        case WM_DF_STOP:    StopCapture(); break;
        case WM_DF_TOGGLE:  ToggleScale(); break;
        case WM_DF_THREADEND:
            if (m_renderThread.joinable()) m_renderThread.join();
            m_threadAlive.store(false);
            TeardownCaptureResources();
            m_state.running.store(false);
            m_fpsHud.Show(false);
            m_panel.ShowPanel(true);
            m_state.SetStatus("Stopped");
            m_panel.SyncFromState();
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (m_state.quit.load()) break;
    }
    StopCapture();
    return 0;
}

} // namespace df
