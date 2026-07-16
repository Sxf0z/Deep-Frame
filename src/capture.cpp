#include "capture.h"

namespace df {
namespace wgc   = winrt::Windows::Graphics::Capture;
namespace wgdx  = winrt::Windows::Graphics::DirectX;
namespace wgd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {

bool IsAltTabWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd)) return false;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, 256);
    if (title[0] == L'\0') return false;
    if (wcsstr(title, L"DeepFrame") != nullptr) return false;
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return false;
    if ((rc.right - rc.left) < 64 || (rc.bottom - rc.top) < 64) return false;
    return true;
}

} // namespace

CaptureSession::CaptureSession(Graphics& gfx) : m_gfx(gfx)
{
    m_frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

CaptureSession::~CaptureSession()
{
    Stop();
    if (m_frameEvent)
    {
        CloseHandle(m_frameEvent);
        m_frameEvent = nullptr;
    }
}

std::vector<WindowInfo> CaptureSession::EnumerateWindows()
{
    std::vector<WindowInfo> list;
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* out = reinterpret_cast<std::vector<WindowInfo>*>(lp);
        if (!IsAltTabWindow(hwnd)) return TRUE;
        WindowInfo info;
        info.hwnd = hwnd;
        wchar_t title[256]{}, cls[128]{};
        GetWindowTextW(hwnd, title, 256);
        GetClassNameW(hwnd, cls, 128);
        info.title = title;
        info.className = cls;
        out->push_back(std::move(info));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&list));
    return list;
}

void CaptureSession::OnFrameArrived(
    wgc::Direct3D11CaptureFramePool const&,
    winrt::Windows::Foundation::IInspectable const&)
{
    if (!m_active.load(std::memory_order_acquire)) return;
    if (m_frameEvent) SetEvent(m_frameEvent);
}

bool CaptureSession::Start(HWND targetWindow)
{
    Stop();
    if (!targetWindow || !IsWindow(targetWindow))
        return false;

    m_target = targetWindow;

    winrt::com_ptr<IDXGIDevice> dxgiDev;
    HRESULT hr = m_gfx.Device()->QueryInterface(__uuidof(IDXGIDevice), dxgiDev.put_void());
    if (FAILED(hr)) return false;

    winrt::com_ptr<::IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDev.get(), inspectable.put());
    if (FAILED(hr)) return false;
    m_winrtDevice = inspectable.as<wgd3d::IDirect3DDevice>();

    auto factory = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    hr = factory->CreateForWindow(
        targetWindow,
        winrt::guid_of<wgc::GraphicsCaptureItem>(),
        winrt::put_abi(m_item));
    if (FAILED(hr))
    {
        Log("CreateForWindow failed 0x%08X", (unsigned)hr);
        return false;
    }

    auto size = m_item.Size();
    m_width  = static_cast<UINT>(std::max(1, size.Width));
    m_height = static_cast<UINT>(std::max(1, size.Height));
    if (m_width * m_height > 3840u * 2160u)
    {
        Stop();
        return false;
    }

    // 3 buffers so we can drain backlog without stalling the producer
    m_pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
        m_winrtDevice,
        wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        3,
        size);

    m_revoker = m_pool.FrameArrived(winrt::auto_revoke, { this, &CaptureSession::OnFrameArrived });
    m_session = m_pool.CreateCaptureSession(m_item);
    try { m_session.IsBorderRequired(false); } catch (...) {}
    try { m_session.IsCursorCaptureEnabled(false); } catch (...) {}

    m_active.store(true, std::memory_order_release);
    m_session.StartCapture();
    Log("WGC started HWND=%p %ux%u", targetWindow, m_width, m_height);
    return true;
}

void CaptureSession::Stop()
{
    m_active.store(false, std::memory_order_release);
    m_revoker.revoke();

    std::lock_guard lock(m_poolMutex);
    try
    {
        if (m_session) { m_session.Close(); m_session = nullptr; }
        if (m_pool)    { m_pool.Close();    m_pool = nullptr; }
    }
    catch (...) {}

    m_item = nullptr;
    m_winrtDevice = nullptr;
    m_target = nullptr;
    m_width = m_height = 0;
    if (m_frameEvent) ResetEvent(m_frameEvent);
}

bool CaptureSession::TryAcquireLatest(ID3D11Texture2D* destColor, UINT& outW, UINT& outH)
{
    if (!destColor || !m_active.load(std::memory_order_acquire)) return false;

    std::lock_guard lock(m_poolMutex);
    if (!m_pool) return false;

    // Drain to newest frame — critical so we don't process stale backlog (halves FPS)
    wgc::Direct3D11CaptureFrame latest{ nullptr };
    for (;;)
    {
        auto f = m_pool.TryGetNextFrame();
        if (!f) break;
        if (latest) try { latest.Close(); } catch (...) {}
        latest = f;
    }
    if (!latest) return false;

    auto contentSize = latest.ContentSize();
    UINT nw = static_cast<UINT>(std::max(1, contentSize.Width));
    UINT nh = static_cast<UINT>(std::max(1, contentSize.Height));
    if (nw != m_width || nh != m_height)
    {
        m_width = nw;
        m_height = nh;
        try
        {
            m_pool.Recreate(
                m_winrtDevice,
                wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                3,
                contentSize);
        }
        catch (...)
        {
            try { latest.Close(); } catch (...) {}
            return false;
        }
    }

    auto surface = latest.Surface();
    auto access = surface.as<IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> captured;
    HRESULT hr = access->GetInterface(__uuidof(ID3D11Texture2D), captured.put_void());
    if (FAILED(hr) || !captured)
    {
        try { latest.Close(); } catch (...) {}
        return false;
    }

    D3D11_TEXTURE2D_DESC srcDesc{}, dstDesc{};
    captured->GetDesc(&srcDesc);
    destColor->GetDesc(&dstDesc);

    if (srcDesc.Width == dstDesc.Width && srcDesc.Height == dstDesc.Height)
        m_gfx.Context()->CopyResource(destColor, captured.get());
    else
    {
        D3D11_BOX box{};
        box.right  = std::min(srcDesc.Width, dstDesc.Width);
        box.bottom = std::min(srcDesc.Height, dstDesc.Height);
        box.back = 1;
        m_gfx.Context()->CopySubresourceRegion(destColor, 0, 0, 0, 0, captured.get(), 0, &box);
    }

    outW = m_width;
    outH = m_height;
    try { latest.Close(); } catch (...) {}
    return true;
}

} // namespace df
