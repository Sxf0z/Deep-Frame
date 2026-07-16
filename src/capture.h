#pragma once
#include "common.h"
#include "graphics.h"

namespace df {

class CaptureSession
{
public:
    explicit CaptureSession(Graphics& gfx);
    ~CaptureSession();

    bool Start(HWND targetWindow);
    void Stop();

    // Drain pool to latest frame (drops backlog so FG never multiplies lag).
    bool TryAcquireLatest(ID3D11Texture2D* destColor, UINT& outW, UINT& outH);

    HANDLE FrameEvent() const { return m_frameEvent; }
    UINT Width()  const { return m_width; }
    UINT Height() const { return m_height; }
    bool IsActive() const { return m_active.load(); }

    static std::vector<WindowInfo> EnumerateWindows();

private:
    Graphics& m_gfx;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_pool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker m_revoker;

    HANDLE m_frameEvent = nullptr;
    UINT m_width = 0, m_height = 0;
    HWND m_target = nullptr;
    std::atomic<bool> m_active{false};
    std::mutex m_poolMutex;

    void OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const&,
        winrt::Windows::Foundation::IInspectable const&);
};

} // namespace df
