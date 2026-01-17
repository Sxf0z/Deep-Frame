#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>

namespace DeepFrame {

using Microsoft::WRL::ComPtr;

struct CapturedFrame {
    ComPtr<ID3D11Texture2D> texture;
    uint32_t width;
    uint32_t height;
    int64_t timestampQpc;
    bool cursorVisible;
    int32_t cursorX;
    int32_t cursorY;
};

enum class CaptureResult : uint8_t {
    Success,
    Timeout,
    AccessLost,
    DeviceLost,
    InvalidCall,
    Uninitialized
};

class DxgiCapture final {
public:
    DxgiCapture() noexcept = default;
    ~DxgiCapture() noexcept;

    DxgiCapture(const DxgiCapture&) = delete;
    DxgiCapture& operator=(const DxgiCapture&) = delete;
    DxgiCapture(DxgiCapture&&) noexcept;
    DxgiCapture& operator=(DxgiCapture&&) noexcept;

    [[nodiscard]] bool Initialize(uint32_t adapterIndex = 0, uint32_t outputIndex = 0) noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] CaptureResult AcquireFrame(CapturedFrame& frame, uint32_t timeoutMs = 0) noexcept;
    void ReleaseFrame() noexcept;

    [[nodiscard]] ID3D11Device* GetDevice() const noexcept { return device_.Get(); }
    [[nodiscard]] ID3D11DeviceContext* GetContext() const noexcept { return context_.Get(); }
    [[nodiscard]] uint32_t GetWidth() const noexcept { return width_; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return height_; }
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    [[nodiscard]] bool CreateD3D11Device(uint32_t adapterIndex) noexcept;
    [[nodiscard]] bool CreateDuplicationOutput(uint32_t outputIndex) noexcept;
    [[nodiscard]] bool ReinitializeDuplication() noexcept;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<IDXGIOutput1> output_;
    ComPtr<IDXGIAdapter1> adapter_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t outputIndex_ = 0;
    bool initialized_ = false;
    bool frameAcquired_ = false;
};

} 