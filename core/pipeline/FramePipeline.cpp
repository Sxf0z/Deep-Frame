
#include "FramePipeline.h"
#include <chrono>

namespace DeepFrame {

FramePipeline::~FramePipeline() noexcept { Shutdown(); }

bool FramePipeline::Initialize(const PipelineConfig &config) noexcept {
  if (initialized_)
    return true;

  config_ = config;

  if (!capture_.Initialize(0, 0)) {
    return false;
  }

  uint32_t width = capture_.GetWidth();
  uint32_t height = capture_.GetHeight();
  ID3D11Device *device = capture_.GetDevice();

  if (!presenter_.Initialize(device, capture_.GetContext(), width, height)) {
    capture_.Shutdown();
    return false;
  }

  if (!captureBuffer_.Initialize(device, width, height) ||
      !interpolatedBuffer_.Initialize(device, width, height)) {
    presenter_.Shutdown();
    capture_.Shutdown();
    return false;
  }

  if (!config_.modelPath.empty()) {
    inference_.Initialize(device, config_.modelPath, config_.mode);
  }

  if (config_.targetWindow) {
    presenter_.SetTargetWindow(config_.targetWindow);
  }
  presenter_.SetShowStats(config_.showStats);

  initialized_ = true;
  return true;
}

void FramePipeline::Shutdown() noexcept {
  Stop();
  captureBuffer_.Shutdown();
  interpolatedBuffer_.Shutdown();
  inference_.Shutdown();
  presenter_.Shutdown();
  capture_.Shutdown();
  initialized_ = false;
}

bool FramePipeline::Start() noexcept {
  if (!initialized_ || running_) {
    return false;
  }

  running_ = true;
  capturedFrames_ = 0;
  presentedFrames_ = 0;

  {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = PipelineStats{};
  }

  presenter_.Show();

  captureThread_ = std::thread(&FramePipeline::CaptureThread, this);
  inferenceThread_ = std::thread(&FramePipeline::InferenceThread, this);
  presentThread_ = std::thread(&FramePipeline::PresentThread, this);

  return true;
}

void FramePipeline::Stop() noexcept {
  if (!running_)
    return;

  running_ = false;

  if (captureThread_.joinable())
    captureThread_.join();
  if (inferenceThread_.joinable())
    inferenceThread_.join();
  if (presentThread_.joinable())
    presentThread_.join();

  presenter_.Hide();
}

void FramePipeline::SetTargetWindow(HWND target) noexcept {
  config_.targetWindow = target;
  presenter_.SetTargetWindow(target);
}

void FramePipeline::SetShowStats(bool show) noexcept {
  config_.showStats = show;
  presenter_.SetShowStats(show);
}

bool FramePipeline::SetMode(InterpolationMode mode,
                            const std::wstring &modelPath) noexcept {
  config_.mode = mode;
  config_.modelPath = modelPath;
  if (initialized_) {
    return inference_.SetMode(mode, modelPath);
  }
  return true;
}

PipelineStats FramePipeline::GetStats() const noexcept {
  std::lock_guard<std::mutex> lock(statsMutex_);
  return stats_;
}

void FramePipeline::CaptureThread() noexcept {
  while (running_) {
    CapturedFrame frame{};
    auto result = capture_.AcquireFrame(frame, 10);

    if (result == CaptureResult::Success && frame.texture) {
      captureBuffer_.Push(capture_.GetContext(), frame.texture.Get(),
                          frame.timestampQpc);
      capturedFrames_++;
    } else if (result == CaptureResult::AccessLost ||
               result == CaptureResult::DeviceLost) {
      break;
    }
  }
}

void FramePipeline::InferenceThread() noexcept {
  ID3D11Texture2D *prevFrame = nullptr;
  uint64_t prevTs = 0;
  ID3D11Texture2D *currFrame = nullptr;
  uint64_t currTs = 0;

  while (running_) {
    ID3D11Texture2D *nextFrame = nullptr;
    uint64_t nextTs = 0;
    if (captureBuffer_.Pop(&nextFrame, &nextTs)) {
      if (currFrame) {
        prevFrame = currFrame;
        prevTs = currTs;
      }
      currFrame = nextFrame;
      currTs = nextTs;

      if (prevFrame && currFrame) {
        if (!interpolatedFrame_) {
          D3D11_TEXTURE2D_DESC desc;
          currFrame->GetDesc(&desc);
          capture_.GetDevice()->CreateTexture2D(&desc, nullptr,
                                                &interpolatedFrame_);
        }

        bool generated = false;
        if (inference_.IsInitialized()) {
          generated = inference_.Interpolate(prevFrame, currFrame,
                                             interpolatedFrame_.Get(), 0.5f);
        }

        if (!generated && interpolatedFrame_) {
          capture_.GetContext()->CopyResource(interpolatedFrame_.Get(),
                                              currFrame);
          generated = true;
        }

        if (generated) {
          interpolatedBuffer_.Push(capture_.GetContext(),
                                   interpolatedFrame_.Get(),
                                   (prevTs + currTs) / 2);
        }

        interpolatedBuffer_.Push(capture_.GetContext(), currFrame, currTs);
      } else if (currFrame) {
        interpolatedBuffer_.Push(capture_.GetContext(), currFrame, currTs);
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}

void FramePipeline::PresentThread() noexcept {
  auto lastTime = std::chrono::high_resolution_clock::now();
  uint64_t frames = 0;

  while (running_) {
    ID3D11Texture2D *frame = nullptr;
    uint64_t ts = 0;
    if (interpolatedBuffer_.Pop(&frame, &ts)) {
      int baseFps, visualFps;
      float latency;
      {
        std::lock_guard<std::mutex> lock(statsMutex_);
        baseFps = static_cast<int>(stats_.captureFps);
        visualFps = static_cast<int>(stats_.presentFps);
        latency = stats_.inferenceTimeMs;
      }

      presenter_.DrawStats(baseFps, visualFps, latency);
      presenter_.PresentFrame(frame);
      presentedFrames_++;
      frames++;

      auto now = std::chrono::high_resolution_clock::now();
      auto elapsed = std::chrono::duration<double>(now - lastTime).count();
      if (elapsed >= 1.0) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.captureFps =
            static_cast<float>(capturedFrames_.exchange(0) / elapsed);
        stats_.presentFps = static_cast<float>(frames / elapsed);
        stats_.inferenceTimeMs = inference_.GetStats().lastInferenceMs;
        frames = 0;
        lastTime = now;
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}

void FramePipeline::WorkerLoop() noexcept {}

} // namespace DeepFrame
