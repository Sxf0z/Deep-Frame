#pragma once
#include "common.h"

namespace df {

// Top-left HUD: countdown ("5") or FPS ("60 / 120") white + black outline.
class FpsHud
{
public:
    bool Create(HINSTANCE inst, RuntimeState& state);
    void Destroy();
    void Show(bool visible);
    void ForceRedraw();

    HWND Hwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Paint(HDC hdc, RECT rc);
    void Reposition();

    RuntimeState* m_state = nullptr;
    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    HFONT m_fontBig = nullptr;
    HFONT m_fontFps = nullptr;
    bool m_visible = false;
};

} // namespace df
