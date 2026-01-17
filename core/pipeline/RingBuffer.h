#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>


namespace DeepFrame {

using Microsoft::WRL::ComPtr;



template <size_t SIZE = 3> class RingBuffer {
public:
  struct Slot {
    ComPtr<ID3D11Texture2D> texture;
    uint64_t timestamp = 0;
    bool valid = false;
  };

  RingBuffer() = default;
  ~RingBuffer() = default;

  
  [[nodiscard]] bool
  Initialize(ID3D11Device *device, uint32_t width, uint32_t height,
             DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM) noexcept {
    device_ = device;
    width_ = width;
    height_ = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    for (size_t i = 0; i < SIZE; i++) {
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &slots_[i].texture))) {
        return false;
      }
      slots_[i].valid = false;
      slots_[i].timestamp = 0;
    }

    writeIndex_ = 0;
    readIndex_ = 0;
    count_ = 0;
    return true;
  }

  void Shutdown() noexcept {
    for (size_t i = 0; i < SIZE; i++) {
      slots_[i].texture.Reset();
      slots_[i].valid = false;
    }
  }

  
  
  [[nodiscard]] bool Push(ID3D11DeviceContext *context, ID3D11Texture2D *frame,
                          uint64_t timestamp) noexcept {
    if (IsFull()) {
      return false; 
    }

    size_t idx = writeIndex_.load(std::memory_order_relaxed);

    
    context->CopyResource(slots_[idx].texture.Get(), frame);
    slots_[idx].timestamp = timestamp;
    slots_[idx].valid = true;

    
    writeIndex_.store((idx + 1) % SIZE, std::memory_order_release);
    count_.fetch_add(1, std::memory_order_release);

    return true;
  }

  
  
  [[nodiscard]] bool Pop(ID3D11Texture2D **outFrame,
                         uint64_t *outTimestamp) noexcept {
    if (IsEmpty()) {
      return false;
    }

    size_t idx = readIndex_.load(std::memory_order_relaxed);

    if (!slots_[idx].valid) {
      return false;
    }

    *outFrame = slots_[idx].texture.Get();
    *outTimestamp = slots_[idx].timestamp;
    slots_[idx].valid = false;

    
    readIndex_.store((idx + 1) % SIZE, std::memory_order_release);
    count_.fetch_sub(1, std::memory_order_release);

    return true;
  }

  
  [[nodiscard]] ID3D11Texture2D *PeekLatest() noexcept {
    if (IsEmpty())
      return nullptr;

    
    size_t idx =
        (writeIndex_.load(std::memory_order_acquire) + SIZE - 1) % SIZE;
    return slots_[idx].valid ? slots_[idx].texture.Get() : nullptr;
  }

  [[nodiscard]] bool IsFull() const noexcept {
    return count_.load(std::memory_order_acquire) >= SIZE;
  }

  [[nodiscard]] bool IsEmpty() const noexcept {
    return count_.load(std::memory_order_acquire) == 0;
  }

  [[nodiscard]] size_t Count() const noexcept {
    return count_.load(std::memory_order_acquire);
  }

private:
  Slot slots_[SIZE];
  ID3D11Device *device_ = nullptr;

  std::atomic<size_t> writeIndex_{0};
  std::atomic<size_t> readIndex_{0};
  std::atomic<size_t> count_{0};

  uint32_t width_ = 0;
  uint32_t height_ = 0;
};

} 