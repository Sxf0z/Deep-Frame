#include "ui.h"

namespace df {
namespace {

constexpr int kPad = 16;
constexpr int kRowH = 28;
constexpr wchar_t kClassPanel[] = L"DeepFrame.ControlPanel";
constexpr wchar_t kClassOverlay[] = L"DeepFrame.Overlay";

// Dark theme palette
constexpr COLORREF kBg       = RGB(22, 22, 28);
constexpr COLORREF kPanel    = RGB(32, 32, 40);
constexpr COLORREF kText     = RGB(230, 230, 235);
constexpr COLORREF kMuted    = RGB(150, 150, 165);
constexpr COLORREF kAccent   = RGB(88, 140, 255);
constexpr COLORREF kBorder   = RGB(50, 50, 62);

HBRUSH g_brBg = nullptr;
HBRUSH g_brPanel = nullptr;
HFONT  g_font = nullptr;
HFONT  g_fontTitle = nullptr;

void EnsureTheme()
{
    if (!g_brBg) g_brBg = CreateSolidBrush(kBg);
    if (!g_brPanel) g_brPanel = CreateSolidBrush(kPanel);
    if (!g_font)
    {
        g_font = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_fontTitle = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }
}

void ApplyFont(HWND hwnd, bool title = false)
{
    EnsureTheme();
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)(title ? g_fontTitle : g_font), TRUE);
}

} // namespace

bool IsDeepFrameWindow(HWND hwnd)
{
    if (!hwnd) return true;
    wchar_t title[256]{}, cls[128]{};
    GetWindowTextW(hwnd, title, 256);
    GetClassNameW(hwnd, cls, 128);
    if (wcsstr(title, L"DeepFrame") != nullptr) return true;
    if (wcsstr(cls, L"DeepFrame") != nullptr) return true;
    return false;
}

HWND ResolveForegroundTarget()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    HWND root = GetAncestor(fg, GA_ROOT);
    if (root) fg = root;
    if (IsDeepFrameWindow(fg)) return nullptr;
    if (!IsWindowVisible(fg) || IsIconic(fg)) return nullptr;
    RECT rc{};
    if (!GetClientRect(fg, &rc)) return nullptr;
    if ((rc.right - rc.left) < 64 || (rc.bottom - rc.top) < 64) return nullptr;
    return fg;
}

bool ControlPanel::Create(HINSTANCE inst, RuntimeState& state)
{
    m_inst = inst;
    m_state = &state;
    EnsureTheme();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = kClassPanel;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // Center on primary work area so the panel is hard to miss
    const int winW = 520, winH = 600;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + (std::max)(0, (int)((wa.right - wa.left) - winW) / 2);
    int y = wa.top + (std::max)(0, (int)((wa.bottom - wa.top) - winH) / 2);

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, kClassPanel,
        L"DeepFrame",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, winW, winH,
        nullptr, nullptr, inst, this);

    if (!m_hwnd) return false;

    // Dark title bar (Win10 1809+)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    SetWindowPos(m_hwnd, HWND_TOP, x, y, winW, winH, SWP_SHOWWINDOW);
    SetForegroundWindow(m_hwnd);
    UpdateWindow(m_hwnd);
    return true;
}

void ControlPanel::Destroy()
{
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

void ControlPanel::ShowPanel(bool show)
{
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, show ? SW_SHOW : SW_HIDE);
    if (show) { SetForegroundWindow(m_hwnd); SyncFromState(); }
}

void ControlPanel::OnCreate()
{
    EnsureTheme();
    int y = kPad;

    m_lblHint = CreateWindowExW(0, L"STATIC",
        L"DeepFrame is OPEN — use Scale / F6 then click your game\n"
        L"Pick RTX GPU  ·  Performance ON  ·  x2  ·  Flow 50%\n"
        L"F6 stop  ·  Ctrl+Shift+F toggle FG",
        WS_CHILD | WS_VISIBLE, kPad, y, 480, 58, m_hwnd, nullptr, m_inst, nullptr);
    ApplyFont(m_lblHint, true);
    y += 64;

    // GPU selector (Iris Xe vs RTX 3050 Ti) — critical on dual-GPU laptops
    m_lblGpu = CreateWindowExW(0, L"STATIC", L"GPU (preferred: RTX 3050 Ti)",
        WS_CHILD | WS_VISIBLE, kPad, y, 280, 18, m_hwnd, nullptr, m_inst, nullptr);
    ApplyFont(m_lblGpu);
    y += 22;
    m_cmbGpu = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        kPad, y, 480, 200, m_hwnd, (HMENU)1008, m_inst, nullptr);
    ApplyFont(m_cmbGpu);
    y += 40;

    CreateWindowExW(0, L"STATIC", L"Windows (optional fallback)",
        WS_CHILD | WS_VISIBLE, kPad, y, 200, 18, m_hwnd, nullptr, m_inst, nullptr);
    y += 22;

    m_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        kPad, y, 480, 120, m_hwnd, (HMENU)1001, m_inst, nullptr);
    ApplyFont(m_list);
    y += 132;

    m_btnRefresh = CreateWindowExW(0, L"BUTTON", L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        kPad, y, 88, kRowH, m_hwnd, (HMENU)1002, m_inst, nullptr);

    m_chkFg = CreateWindowExW(0, L"BUTTON", L"Frame generation",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        kPad + 100, y, 140, kRowH, m_hwnd, (HMENU)1003, m_inst, nullptr);
    SendMessageW(m_chkFg, BM_SETCHECK, BST_CHECKED, 0);

    m_chkPerf = CreateWindowExW(0, L"BUTTON", L"Performance (low lag)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        kPad + 250, y, 170, kRowH, m_hwnd, (HMENU)1004, m_inst, nullptr);
    SendMessageW(m_chkPerf, BM_SETCHECK, BST_CHECKED, 0);
    y += 38;

    m_lblMult = CreateWindowExW(0, L"STATIC", L"Multiplier",
        WS_CHILD | WS_VISIBLE, kPad, y + 4, 80, 20, m_hwnd, nullptr, m_inst, nullptr);
    m_cmbMult = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        kPad + 90, y, 90, 200, m_hwnd, (HMENU)1005, m_inst, nullptr);
    SendMessageW(m_cmbMult, CB_ADDSTRING, 0, (LPARAM)L"x2");
    SendMessageW(m_cmbMult, CB_ADDSTRING, 0, (LPARAM)L"x3");
    SendMessageW(m_cmbMult, CB_ADDSTRING, 0, (LPARAM)L"x4");
    SendMessageW(m_cmbMult, CB_SETCURSEL, 0, 0);

    m_lblFlow = CreateWindowExW(0, L"STATIC", L"Flow scale",
        WS_CHILD | WS_VISIBLE, kPad + 200, y + 4, 80, 20, m_hwnd, nullptr, m_inst, nullptr);
    m_cmbFlow = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        kPad + 280, y, 120, 200, m_hwnd, (HMENU)1006, m_inst, nullptr);
    SendMessageW(m_cmbFlow, CB_ADDSTRING, 0, (LPARAM)L"25% fastest");
    SendMessageW(m_cmbFlow, CB_ADDSTRING, 0, (LPARAM)L"40%");
    SendMessageW(m_cmbFlow, CB_ADDSTRING, 0, (LPARAM)L"50% balanced");
    SendMessageW(m_cmbFlow, CB_ADDSTRING, 0, (LPARAM)L"75% quality");
    SendMessageW(m_cmbFlow, CB_SETCURSEL, 2, 0); // 50% default
    y += 42;

    m_chkUpscale = CreateWindowExW(0, L"BUTTON", L"Sharpen (adds GPU cost)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        kPad, y, 220, kRowH, m_hwnd, (HMENU)1007, m_inst, nullptr);
    y += 42;

    m_btnStart = CreateWindowExW(0, L"BUTTON", L"Scale  (F6)",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        kPad, y, 130, 38, m_hwnd, (HMENU)1010, m_inst, nullptr);
    m_btnStop = CreateWindowExW(0, L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        kPad + 144, y, 90, 38, m_hwnd, (HMENU)1011, m_inst, nullptr);
    EnableWindow(m_btnStop, FALSE);

    m_status = CreateWindowExW(0, L"STATIC", L"Ready",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        kPad + 250, y + 6, 200, 36, m_hwnd, nullptr, m_inst, nullptr);

    for (HWND c : { m_btnRefresh, m_chkFg, m_chkPerf, m_chkUpscale, m_cmbMult, m_cmbFlow,
                    m_lblMult, m_lblFlow, m_lblGpu, m_cmbGpu, m_btnStart, m_btnStop, m_status })
        if (c) ApplyFont(c);
}

void ControlPanel::SetWindowList(const std::vector<WindowInfo>& windows)
{
    m_windows = windows;
    if (!m_list) return;
    SendMessageW(m_list, LB_RESETCONTENT, 0, 0);
    for (auto& w : m_windows)
    {
        std::wstring line = w.title;
        if (line.size() > 68) line = line.substr(0, 65) + L"...";
        SendMessageW(m_list, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

void ControlPanel::SetGpuList(const std::vector<GpuInfo>& gpus, int selectedAdapterIndex)
{
    m_gpus = gpus;
    if (!m_cmbGpu) return;
    SendMessageW(m_cmbGpu, CB_RESETCONTENT, 0, 0);

    int sel = 0;
    for (int i = 0; i < (int)m_gpus.size(); ++i)
    {
        auto& g = m_gpus[i];
        double mb = g.dedicatedBytes / (1024.0 * 1024.0);
        wchar_t line[320];
        const wchar_t* tag = g.isNvidia ? L"[NVIDIA]" : (g.isIntel ? L"[Intel]" : (g.isAmd ? L"[AMD]" : L"[GPU]"));
        swprintf_s(line, L"%s  %s  (%.0f MB)", tag, g.name.c_str(), mb);
        SendMessageW(m_cmbGpu, CB_ADDSTRING, 0, (LPARAM)line);
        // Store adapter index in item data
        SendMessageW(m_cmbGpu, CB_SETITEMDATA, i, (LPARAM)g.index);
        if (g.index == selectedAdapterIndex)
            sel = i;
    }
    if (!m_gpus.empty())
        SendMessageW(m_cmbGpu, CB_SETCURSEL, sel, 0);
}

int ControlPanel::SelectedGpuAdapterIndex() const
{
    if (!m_cmbGpu || m_gpus.empty()) return 0;
    int i = (int)SendMessageW(m_cmbGpu, CB_GETCURSEL, 0, 0);
    if (i < 0 || i >= (int)m_gpus.size()) return m_gpus.front().index;
    return (int)SendMessageW(m_cmbGpu, CB_GETITEMDATA, i, 0);
}

HWND ControlPanel::SelectedTarget() const
{
    if (!m_list) return nullptr;
    int i = (int)SendMessageW(m_list, LB_GETCURSEL, 0, 0);
    if (i < 0 || i >= (int)m_windows.size()) return nullptr;
    return m_windows[i].hwnd;
}

void ControlPanel::SetStatusText(const wchar_t* text)
{
    if (m_status) SetWindowTextW(m_status, text);
}

void ControlPanel::ReadControlsToState()
{
    if (!m_state) return;
    m_state->frameGen.store(SendMessageW(m_chkFg, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_state->performanceMode.store(SendMessageW(m_chkPerf, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_state->upscale.store(SendMessageW(m_chkUpscale, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_state->gpuAdapterIndex.store(SelectedGpuAdapterIndex());
    int mi = (int)SendMessageW(m_cmbMult, CB_GETCURSEL, 0, 0);
    m_state->multiplier.store(mi + 2);
    int fi = (int)SendMessageW(m_cmbFlow, CB_GETCURSEL, 0, 0);
    const int pct[] = { 25, 40, 50, 75 };
    if (fi >= 0 && fi < 4)
        m_state->flowScalePct.store(pct[fi]);
    else
        m_state->flowScalePct.store(50);
}

void ControlPanel::SyncFromState()
{
    if (!m_state) return;
    bool run = m_state->running.load();
    EnableWindow(m_btnStart, !run);
    EnableWindow(m_btnStop, run);
    EnableWindow(m_list, !run);
    EnableWindow(m_btnRefresh, !run);
    EnableWindow(m_cmbGpu, !run); // GPU switch only when idle

    char st[256];
    {
        std::lock_guard lock(m_state->statusMutex);
        snprintf(st, sizeof(st), "%s", m_state->status);
    }
    wchar_t wbuf[256];
    MultiByteToWideChar(CP_UTF8, 0, st, -1, wbuf, 256);
    SetStatusText(wbuf);
}

void ControlPanel::OnCommand(WPARAM wp, LPARAM)
{
    const int id = LOWORD(wp);
    const int code = HIWORD(wp);
    ReadControlsToState();
    switch (id)
    {
    case 1002: if (code == BN_CLICKED) PostMessageW(m_hwnd, WM_DF_REFRESH, 0, 0); break;
    case 1010: if (code == BN_CLICKED) PostMessageW(m_hwnd, WM_DF_SCALE, 0, 0); break;
    case 1011: if (code == BN_CLICKED) PostMessageW(m_hwnd, WM_DF_STOP, 0, 0); break;
    }
}

LRESULT CALLBACK ControlPanel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ControlPanel* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ControlPanel*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
        self = reinterpret_cast<ControlPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_CREATE:
        if (self) self->OnCreate();
        return 0;
    case WM_COMMAND:
        if (self) self->OnCommand(wp, lp);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        EnsureTheme();
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, kText);
        SetBkColor(hdc, kBg);
        return (LRESULT)g_brBg;
    }
    case WM_CTLCOLORLISTBOX:
    {
        EnsureTheme();
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, kText);
        SetBkColor(hdc, kPanel);
        return (LRESULT)g_brPanel;
    }
    case WM_ERASEBKGND:
    {
        EnsureTheme();
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, g_brBg);
        return 1;
    }
    case WM_CLOSE:
        if (self && self->m_state) self->m_state->quit.store(true);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Overlay ──────────────────────────────────────────────────────────────────

bool OverlayWindow::Create(HINSTANCE inst)
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassOverlay;
    RegisterClassExW(&wc);

    const DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST
                   | WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

    m_hwnd = CreateWindowExW(ex, kClassOverlay, L"DeepFrame Overlay",
        WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, inst, nullptr);
    if (!m_hwnd) return false;
    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(m_hwnd, SW_HIDE);
    return true;
}

void OverlayWindow::Destroy()
{
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

void OverlayWindow::ShowOver(HWND target)
{
    m_hasLast = false;
    if (!m_hwnd || !target) return;
    FollowTarget(target);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
}

void OverlayWindow::Hide()
{
    if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
    m_hasLast = false;
}

void OverlayWindow::FollowTarget(HWND target)
{
    if (!m_hwnd || !IsWindow(target)) return;
    RECT rc{};
    if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc))))
        if (!GetWindowRect(target, &rc)) return;

    if (m_hasLast &&
        rc.left == m_lastRc.left && rc.top == m_lastRc.top &&
        rc.right == m_lastRc.right && rc.bottom == m_lastRc.bottom)
        return;

    m_lastRc = rc;
    m_hasLast = true;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 1 || h < 1) return;
    SetWindowPos(m_hwnd, HWND_TOPMOST, rc.left, rc.top, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace df
