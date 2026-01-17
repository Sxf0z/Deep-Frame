#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>
#include <d2d1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>


namespace DeepFrame {

using Microsoft::WRL::ComPtr;

class FramePresenter {
public:
  FramePresenter() noexcept = default;
  ~FramePresenter() noexcept;

  FramePresenter(const FramePresenter &) = delete;
  FramePresenter &operator=(const FramePresenter &) = delete;

  bool Initialize(ID3D11Device *device, ID3D11DeviceContext *context,
                  uint32_t width, uint32_t height) noexcept;
  void Shutdown() noexcept;

  
  void PresentFrame(ID3D11Texture2D *frame) noexcept;

  
  void SetTargetWindow(HWND target) noexcept;

  
  void Show() noexcept;
  void Hide() noexcept;

  
  void UpdatePosition() noexcept;

  
  void DrawStats(int baseFps, int visualFps, float latencyMs) noexcept;

  void SetShowStats(bool show) noexcept { showStats_ = show; }
  bool IsInitialized() const noexcept { return initialized_; }

private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam);

  bool CreateOverlayWindow() noexcept;
  bool CreateSwapChain() noexcept;
  bool CreateD2DResources() noexcept;

  ID3D11Device *device_ = nullptr;
  ID3D11DeviceContext *context_ = nullptr;

  ComPtr<IDXGISwapChain1> swapChain_;
  ComPtr<ID3D11RenderTargetView> renderTargetView_;
  ComPtr<ID3D11Texture2D> backBuffer_;

  
  ComPtr<ID2D1Factory> d2dFactory_;
  ComPtr<ID2D1RenderTarget> d2dTarget_;
  ComPtr<ID2D1SolidColorBrush> whiteBrush_;
  ComPtr<ID2D1SolidColorBrush> blackBrush_;
  ComPtr<IDWriteFactory> dwriteFactory_;
  ComPtr<IDWriteTextFormat> textFormat_;

  HWND overlayWindow_ = nullptr;
  HWND targetWindow_ = nullptr;
  HINSTANCE hInstance_ = nullptr;

  uint32_t width_ = 0;
  uint32_t height_ = 0;
  bool initialized_ = false;
  bool showStats_ = true;
  bool visible_ = false;

  
  int lastBaseFps_ = 0;
  int lastVisualFps_ = 0;
  float lastLatencyMs_ = 0.0f;
};

} 