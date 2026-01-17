
#include "FramePipeline.h"
#include <chrono>

namespace DeepFrame {

FramePipeline::~FramePipeline() noexcept { Shutdown(); }

bool FramePipeline::Initialize(const PipelineConfig &config) noexcept {
  if (initialized_)
    return true;

  config_ = config;

  
  if (!capture_.Initialize(0, 0)) {
    printf("[FramePipeline] Capture init failed\n");
    return false;
  }

  uint32_t width = capture_.GetWidth();
  uint32_t height = capture_.GetHeight();
  ID3D11Device *device = capture_.GetDevice();

  printf("[FramePipeline] Capture initialized: %dx%d\n", width, height);

  
  if (!presenter_.Initialize(device, capture_.GetContext(), width, height)) {
    printf("[FramePipeline] Presenter init failed\n");
    capture_.Shutdown();
    return false;
  }

  printf("[FramePipeline] Presenter initialized\n");

  if (config_.targetWindow) {
    presenter_.SetTargetWindow(config_.targetWindow);
  }
  presenter_.SetShowStats(config_.showStats);

  initialized_ = true;
  printf("[FramePipeline] Initialization complete\n");
  return true;
}

void FramePipeline::Shutdown() noexcept {
  Stop();

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

  
  workerThread_ = std::thread(&FramePipeline::WorkerLoop, this);

  printf("[FramePipeline] Started\n");
  return true;
}

void FramePipeline::Stop() noexcept {
  if (!running_)
    return;

  running_ = false;

  
  if (workerThread_.joinable())
    workerThread_.join();

  presenter_.Hide();
  printf("[FramePipeline] Stopped\n");
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
  return true; 
}

PipelineStats FramePipeline::GetStats() const noexcept {
  std::lock_guard<std::mutex> lock(statsMutex_);
  return stats_;
}



void FramePipeline::WorkerLoop() noexcept {
  auto lastFpsTime = std::chrono::high_resolution_clock::now();
  uint64_t framesSinceLastFps = 0;
  uint64_t capturesSinceLastFps = 0;

  printf("[FramePipeline] Worker loop started\n");

  while (running_) {
    
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    
    CapturedFrame frame{};
    auto result = capture_.AcquireFrame(frame, 16);

    if (result == CaptureResult::Success) {
      capturesSinceLastFps++;

      
      if (frame.texture) {
        
        int baseFps, visualFps;
        float latency;
        {
          std::lock_guard<std::mutex> lock(statsMutex_);
          baseFps = static_cast<int>(stats_.captureFps);
          visualFps = static_cast<int>(stats_.presentFps);
          latency = stats_.inferenceTimeMs;
        }

        presenter_.DrawStats(baseFps, visualFps, latency);
        presenter_.PresentFrame(frame.texture.Get());

        framesSinceLastFps++;
        presentedFrames_++;
      }

      capture_.ReleaseFrame();
    }

    
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double>(now - lastFpsTime).count();
    if (elapsed >= 1.0) {
      std::lock_guard<std::mutex> lock(statsMutex_);
      stats_.captureFps = static_cast<float>(capturesSinceLastFps / elapsed);
      stats_.presentFps = static_cast<float>(framesSinceLastFps / elapsed);
      capturesSinceLastFps = 0;
      framesSinceLastFps = 0;
      lastFpsTime = now;
    }
  }

  printf("[FramePipeline] Worker loop ended\n");
}


void FramePipeline::CaptureThread() noexcept {}
void FramePipeline::InferenceThread() noexcept {}
void FramePipeline::PresentThread() noexcept {}

} 