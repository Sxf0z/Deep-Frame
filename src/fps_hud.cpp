#include "fps_hud.h"

namespace df {
namespace {
constexpr wchar_t kClass[] = L"DeepFrame.FpsHud";

void DrawOutlinedText(HDC hdc, HFONT font, const wchar_t* text, RECT r)
{
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);

    SetTextColor(hdc, RGB(0, 0, 0));
    const int ox[] = { -2,-1,0,1,2, -2,2,-2,2, -1,1,-1,1, 0,0 };
    const int oy[] = {  0, 0,0,0,0, -2,-2,2,2, -1,-1,1,1,-2,2 };
    for (int i = 0; i < 15; ++i)
    {
        RECT rr = r;
        OffsetRect(&rr, ox[i], oy[i]);
        DrawTextW(hdc, text, -1, &rr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, text, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
}
}

bool FpsHud::Create(HINSTANCE inst, RuntimeState& state)
{
    m_inst = inst;
    m_state = &state;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    m_fontFps = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    m_fontBig = CreateFontW(72, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    const DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST
                   | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    m_hwnd = CreateWindowExW(ex, kClass, L"DeepFrame HUD", WS_POPUP,
        16, 12, 360, 96, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;

    SetLayeredWindowAttributes(m_hwnd, RGB(255, 0, 255), 0, LWA_COLORKEY);
    SetTimer(m_hwnd, 1, 33, nullptr); // ~30 Hz redraw
    ShowWindow(m_hwnd, SW_HIDE);
    return true;
}

void FpsHud::Destroy()
{
    if (m_hwnd)
    {
        KillTimer(m_hwnd, 1);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_fontFps) { DeleteObject(m_fontFps); m_fontFps = nullptr; }
    if (m_fontBig) { DeleteObject(m_fontBig); m_fontBig = nullptr; }
}

void FpsHud::Show(bool visible)
{
    m_visible = visible;
    if (!m_hwnd) return;
    if (visible)
    {
        Reposition();
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        ForceRedraw();
    }
    else
    {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void FpsHud::ForceRedraw()
{
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void FpsHud::Reposition()
{
    if (!m_hwnd) return;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int cd = m_state ? m_state->countdown.load() : -1;
    int w = (cd >= 0) ? 200 : 340;
    int h = (cd >= 0) ? 100 : 52;
    SetWindowPos(m_hwnd, HWND_TOPMOST, wa.left + 16, wa.top + 12, w, h,
                 SWP_NOACTIVATE | (m_visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

void FpsHud::Paint(HDC hdc, RECT rc)
{
    HBRUSH br = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    if (!m_state) return;

    const int cd = m_state->countdown.load();
    RECT r = rc;
    r.left += 6;
    r.top += 4;

    if (cd >= 0)
    {
        // Big countdown number (LS scale delay)
        wchar_t line[32];
        if (cd == 0)
            swprintf_s(line, L"GO");
        else
            swprintf_s(line, L"%d", cd);
        DrawOutlinedText(hdc, m_fontBig, line, r);
        return;
    }

    if (!m_state->running.load())
        return;

    const int src = (int)std::lround(m_state->sourceFps.load());
    const int out = (int)std::lround(m_state->outputFps.load());
    wchar_t line[64];
    // Match LS style: source / generated
    swprintf_s(line, L"%d  /  %d", src, out);
    DrawOutlinedText(hdc, m_fontFps, line, r);
}

LRESULT CALLBACK FpsHud::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    FpsHud* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<FpsHud*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
        self = reinterpret_cast<FpsHud*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_TIMER:
        if (self && self->m_visible)
        {
            self->Reposition();
            self->ForceRedraw();
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            self->Paint(mem, rc);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace df
