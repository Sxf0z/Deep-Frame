#pragma once
#include "common.h"
#include "graphics.h"
#include <condition_variable>

namespace df {

// Lock-free-ish ring of GPU textures for LS-style dual-thread FG:
//   Producer (capture/FG) copies finished frames into slots and schedules times.
//   Consumer (present thread) presents at scheduled QPC deadlines — independent of capture.
// This is what actually multiplies output FPS.

constexpr int kPresentRing = 12;
constexpr int kQueueCap = 24;

struct PresentJob
{
    int      slot = -1;       // index into ring textures
    LONGLONG dueQpc = 0;      // absolute present time
    bool     isGenerated = false;
};

class PresentQueue
{
public:
    bool Init(Graphics& gfx, UINT w, UINT h);
    void Release();
    void Resize(Graphics& gfx, UINT w, UINT h);

    // Producer: copy src into next ring slot, push job. Returns false if queue full (drops).
    bool PushCopy(Graphics& gfx, ID3D11Texture2D* src, LONGLONG dueQpc, bool generated);

    // Consumer: wait for next job (or stop), present it. Returns false if stopping.
    bool PopAndPresent(Graphics& gfx, HANDLE stopEvent, HANDLE hTimer,
                       LARGE_INTEGER freq, bool upscale, float sharpness,
                       bool* outGenerated);

    void Clear();
    int  Depth() const;

    UINT Width()  const { return m_w; }
    UINT Height() const { return m_h; }

private:
    winrt::com_ptr<ID3D11Texture2D>          m_ring[kPresentRing];
    winrt::com_ptr<ID3D11ShaderResourceView> m_srv[kPresentRing];
    UINT m_w = 0, m_h = 0;
    int  m_writeSlot = 0;

    PresentJob m_q[kQueueCap]{};
    int m_qHead = 0, m_qTail = 0, m_qCount = 0;
    mutable std::mutex m_mu;
    std::condition_variable m_cv;
};

} // namespace df
