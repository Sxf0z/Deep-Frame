#pragma once
#include "common.h"
#include "graphics.h"
#include "capture.h"
#include "frame_gen.h"
#include "ui.h"
#include "fps_hud.h"
#include <memory>

namespace df {

class Engine
{
public:
    bool Init(HINSTANCE inst);
    void Shutdown();
    int  Run();

private:
    void RefreshWindowList();
    void BeginScaleSequence();
    void OnCountdownTick();
    void StartCaptureOnTarget(HWND target);
    void StopCapture();
    void ToggleScale();
    void RenderThreadMain(); // single low-latency thread
    void TeardownCaptureResources();
    void RegisterHotkeys();
    void UnregisterHotkeys();

    HINSTANCE     m_inst = nullptr;
    RuntimeState  m_state;
    Graphics      m_gfx;
    ControlPanel  m_panel;
    OverlayWindow m_overlay;
    FpsHud        m_fpsHud;

    std::unique_ptr<CaptureSession>   m_capture;
    std::unique_ptr<FrameGenPipeline> m_fg;
    std::thread   m_renderThread;
    HANDLE        m_stopEvent = nullptr;
    std::atomic<bool> m_threadAlive{false};
    bool m_countdownActive = false;
};

} // namespace df
