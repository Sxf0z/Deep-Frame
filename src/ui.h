#pragma once
#include "common.h"

namespace df {

class ControlPanel
{
public:
    bool Create(HINSTANCE inst, RuntimeState& state);
    void Destroy();
    HWND Hwnd() const { return m_hwnd; }

    void SetWindowList(const std::vector<WindowInfo>& windows);
    void SetGpuList(const std::vector<GpuInfo>& gpus, int selectedAdapterIndex);
    HWND SelectedTarget() const;
    int  SelectedGpuAdapterIndex() const; // DXGI adapter ordinal
    void SetStatusText(const wchar_t* text);
    void SyncFromState();
    void ReadControlsToState();
    void ShowPanel(bool show);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    RuntimeState* m_state = nullptr;
    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;
    HWND m_btnRefresh = nullptr;
    HWND m_btnStart = nullptr;
    HWND m_btnStop = nullptr;
    HWND m_chkFg = nullptr;
    HWND m_chkPerf = nullptr;
    HWND m_chkUpscale = nullptr;
    HWND m_cmbMult = nullptr;
    HWND m_cmbFlow = nullptr;
    HWND m_cmbGpu = nullptr;
    HWND m_status = nullptr;
    HWND m_lblHint = nullptr;
    HWND m_lblMult = nullptr;
    HWND m_lblFlow = nullptr;
    HWND m_lblGpu = nullptr;

    std::vector<WindowInfo> m_windows;
    std::vector<GpuInfo>    m_gpus;

    void OnCreate();
    void OnCommand(WPARAM wp, LPARAM lp);
};

class OverlayWindow
{
public:
    bool Create(HINSTANCE inst);
    void Destroy();
    HWND Hwnd() const { return m_hwnd; }

    void ShowOver(HWND target);
    void Hide();
    void FollowTarget(HWND target);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    HWND m_hwnd = nullptr;
    RECT m_lastRc{};
    bool m_hasLast = false;
};

// Returns true if hwnd is one of DeepFrame's windows (by title/class).
bool IsDeepFrameWindow(HWND hwnd);

// Resolve a good capture target from foreground (LS-style).
HWND ResolveForegroundTarget();

} // namespace df
