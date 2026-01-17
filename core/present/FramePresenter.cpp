

#include "FramePresenter.h"
#include <string>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace DeepFrame {

static const wchar_t *OVERLAY_CLASS_NAME = L"DeepFrameOverlay";
static FramePresenter *g_presenterInstance = nullptr;

FramePresenter::~FramePresenter() noexcept { Shutdown(); }

LRESULT CALLBACK FramePresenter::WindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_DESTROY:
    return 0;
  case WM_NCHITTEST:
    return HTTRANSPARENT; 
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool FramePresenter::Initialize(ID3D11Device *device,
                                ID3D11DeviceContext *context, uint32_t width,
                                uint32_t height) noexcept {
  if (initialized_)
    return true;

  printf("[FramePresenter] Initialize: %ux%u\n", width, height);

  device_ = device;
  context_ = context;
  width_ = width;
  height_ = height;
  hInstance_ = GetModuleHandle(nullptr);
  g_presenterInstance = this;

  if (!CreateOverlayWindow()) {
    printf("[FramePresenter] CreateOverlayWindow FAILED\n");
    return false;
  }
  printf("[FramePresenter] CreateOverlayWindow OK, hwnd=%p\n", overlayWindow_);

  if (!CreateSwapChain()) {
    printf("[FramePresenter] CreateSwapChain FAILED\n");
    Shutdown();
    return false;
  }
  printf("[FramePresenter] CreateSwapChain OK\n");

  if (!CreateD2DResources()) {
    printf("[FramePresenter] CreateD2DResources FAILED\n");
    Shutdown();
    return false;
  }
  printf("[FramePresenter] CreateD2DResources OK\n");

  initialized_ = true;
  printf("[FramePresenter] Initialized successfully\n");
  return true;
}

void FramePresenter::Shutdown() noexcept {
  if (overlayWindow_) {
    DestroyWindow(overlayWindow_);
    overlayWindow_ = nullptr;
  }

  UnregisterClassW(OVERLAY_CLASS_NAME, hInstance_);

  whiteBrush_.Reset();
  blackBrush_.Reset();
  textFormat_.Reset();
  d2dTarget_.Reset();
  dwriteFactory_.Reset();
  d2dFactory_.Reset();
  renderTargetView_.Reset();
  backBuffer_.Reset();
  swapChain_.Reset();

  initialized_ = false;
  g_presenterInstance = nullptr;
}

bool FramePresenter::CreateOverlayWindow() noexcept {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance_;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = OVERLAY_CLASS_NAME;

  if (!RegisterClassExW(&wc)) {
    DWORD err = GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      return false;
    }
  }

  
  overlayWindow_ = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
      OVERLAY_CLASS_NAME, L"Deep Frame Overlay", WS_POPUP, 0, 0, width_,
      height_, nullptr, nullptr, hInstance_, nullptr);

  if (!overlayWindow_) {
    return false;
  }

  
  SetLayeredWindowAttributes(overlayWindow_, 0, 255, LWA_ALPHA);

  return true;
}

bool FramePresenter::CreateSwapChain() noexcept {
  ComPtr<IDXGIDevice> dxgiDevice;
  if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) {
    printf("[FramePresenter] QueryInterface for IDXGIDevice failed\n");
    return false;
  }

  ComPtr<IDXGIAdapter> adapter;
  if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
    printf("[FramePresenter] GetAdapter failed\n");
    return false;
  }

  ComPtr<IDXGIFactory2> factory;
  if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
    printf("[FramePresenter] GetParent for IDXGIFactory2 failed\n");
    return false;
  }

  
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = width_;
  desc.Height = height_;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect =
      DXGI_SWAP_EFFECT_FLIP_DISCARD;       
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE; 
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.Flags = 0;

  HRESULT hr = factory->CreateSwapChainForHwnd(device_, overlayWindow_, &desc,
                                               nullptr, nullptr, &swapChain_);
  if (FAILED(hr)) {
    printf("[FramePresenter] CreateSwapChainForHwnd failed: 0x%lx\n", hr);
    return false;
  }

  
  hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer_));
  if (FAILED(hr)) {
    printf("[FramePresenter] GetBuffer failed: 0x%lx\n", hr);
    return false;
  }

  
  hr = device_->CreateRenderTargetView(backBuffer_.Get(), nullptr,
                                       &renderTargetView_);
  if (FAILED(hr)) {
    printf("[FramePresenter] CreateRenderTargetView failed: 0x%lx\n", hr);
    return false;
  }

  return true;
}

bool FramePresenter::CreateD2DResources() noexcept {
  
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               IID_PPV_ARGS(&d2dFactory_)))) {
    return false;
  }

  
  if (FAILED(DWriteCreateFactory(
          DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
          reinterpret_cast<IUnknown **>(dwriteFactory_.GetAddressOf())))) {
    return false;
  }

  
  if (FAILED(dwriteFactory_->CreateTextFormat(
          L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"en-us",
          &textFormat_))) {
    return false;
  }

  
  ComPtr<IDXGISurface> surface;
  if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)))) {
    return false;
  }

  
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

  if (FAILED(d2dFactory_->CreateDxgiSurfaceRenderTarget(surface.Get(), &props,
                                                        &d2dTarget_))) {
    return false;
  }

  
  if (FAILED(d2dTarget_->CreateSolidColorBrush(
          D2D1::ColorF(D2D1::ColorF::White), &whiteBrush_))) {
    return false;
  }

  if (FAILED(d2dTarget_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.7f),
                                               &blackBrush_))) {
    return false;
  }

  return true;
}

void FramePresenter::SetTargetWindow(HWND target) noexcept {
  targetWindow_ = target;
  UpdatePosition();
}

void FramePresenter::Show() noexcept {
  if (overlayWindow_ && !visible_) {
    ShowWindow(overlayWindow_, SW_SHOWNOACTIVATE);
    visible_ = true;
  }
}

void FramePresenter::Hide() noexcept {
  if (overlayWindow_ && visible_) {
    ShowWindow(overlayWindow_, SW_HIDE);
    visible_ = false;
  }
}

void FramePresenter::UpdatePosition() noexcept {
  if (!overlayWindow_)
    return;

  if (targetWindow_) {
    RECT rect;
    if (GetWindowRect(targetWindow_, &rect)) {
      SetWindowPos(overlayWindow_, HWND_TOPMOST, rect.left, rect.top,
                   rect.right - rect.left, rect.bottom - rect.top,
                   SWP_NOACTIVATE);
    }
  } else {
    
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(overlayWindow_, HWND_TOPMOST, 0, 0, screenWidth, screenHeight,
                 SWP_NOACTIVATE);
  }
}

void FramePresenter::PresentFrame(ID3D11Texture2D *frame) noexcept {
  static int presentCount = 0;

  if (!initialized_ || !swapChain_ || !frame) {
    if (presentCount == 0) {
      printf("[FramePresenter] PresentFrame called but not ready: init=%d, "
             "swapChain=%p, frame=%p\n",
             initialized_, swapChain_.Get(), frame);
    }
    return;
  }

  
  if (!visible_) {
    printf("[FramePresenter] Showing overlay window\n");
    Show();
  }

  
  UpdatePosition();

  
  context_->CopyResource(backBuffer_.Get(), frame);

  
  if (showStats_ && d2dTarget_) {
    DrawStats(lastBaseFps_, lastVisualFps_, lastLatencyMs_);
  }

  
  HRESULT hr = swapChain_->Present(1, 0);

  presentCount++;
  if (presentCount == 1 || presentCount % 100 == 0) {
    printf("[FramePresenter] Presented frame #%d, hr=0x%lx\n", presentCount,
           hr);
  }
}

void FramePresenter::DrawStats(int baseFps, int visualFps,
                               float latencyMs) noexcept {
  if (!showStats_ || !d2dTarget_)
    return;

  
  lastBaseFps_ = baseFps;
  lastVisualFps_ = visualFps;
  lastLatencyMs_ = latencyMs;

  d2dTarget_->BeginDraw();

  
  wchar_t text[64];
  swprintf_s(text, L"%d/%d %.1fms", baseFps, visualFps, latencyMs);

  
  D2D1_RECT_F bgRect = D2D1::RectF(10.f, 10.f, 160.f, 45.f);
  d2dTarget_->FillRoundedRectangle(D2D1::RoundedRect(bgRect, 5.f, 5.f),
                                   blackBrush_.Get());

  
  D2D1_RECT_F textRect = D2D1::RectF(18.f, 14.f, 152.f, 41.f);
  d2dTarget_->DrawText(text, static_cast<UINT32>(wcslen(text)),
                       textFormat_.Get(), textRect, whiteBrush_.Get());

  d2dTarget_->EndDraw();
}

} 