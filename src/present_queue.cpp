#include "present_queue.h"

namespace df {

bool PresentQueue::Init(Graphics& gfx, UINT w, UINT h)
{
    Release();
    if (!w || !h) return false;
    m_w = w; m_h = h;
    m_writeSlot = 0;
    m_qHead = m_qTail = m_qCount = 0;

    const UINT bind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    try
    {
        for (int i = 0; i < kPresentRing; ++i)
        {
            m_ring[i] = gfx.CreateTex2D(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, bind);
            ThrowIfFailed(gfx.Device()->CreateShaderResourceView(
                m_ring[i].get(), nullptr, m_srv[i].put()), "ring SRV");
        }
    }
    catch (...)
    {
        Release();
        return false;
    }
    return true;
}

void PresentQueue::Release()
{
    Clear();
    for (int i = 0; i < kPresentRing; ++i)
    {
        m_srv[i] = nullptr;
        m_ring[i] = nullptr;
    }
    m_w = m_h = 0;
}

void PresentQueue::Resize(Graphics& gfx, UINT w, UINT h)
{
    if (w == m_w && h == m_h && m_ring[0]) return;
    Init(gfx, w, h);
}

void PresentQueue::Clear()
{
    std::lock_guard lock(m_mu);
    m_qHead = m_qTail = m_qCount = 0;
    m_cv.notify_all();
}

int PresentQueue::Depth() const
{
    std::lock_guard lock(m_mu);
    return m_qCount;
}

bool PresentQueue::PushCopy(Graphics& gfx, ID3D11Texture2D* src, LONGLONG dueQpc, bool generated)
{
    if (!src || !m_ring[0]) return false;

    int slot;
    {
        std::lock_guard lock(m_mu);
        if (m_qCount >= kQueueCap)
        {
            m_qHead = (m_qHead + 1) % kQueueCap;
            --m_qCount;
        }
        slot = m_writeSlot;
        m_writeSlot = (m_writeSlot + 1) % kPresentRing;
    }

    {
        std::lock_guard lock(gfx.CtxMutex());
        gfx.Context()->CopyResource(m_ring[slot].get(), src);
    }

    {
        std::lock_guard lock(m_mu);
        m_q[m_qTail] = PresentJob{ slot, dueQpc, generated };
        m_qTail = (m_qTail + 1) % kQueueCap;
        ++m_qCount;
    }
    m_cv.notify_one();
    return true;
}

bool PresentQueue::PopAndPresent(Graphics& gfx, HANDLE stopEvent, HANDLE hTimer,
                                 LARGE_INTEGER freq, bool upscale, float sharpness,
                                 bool* outGenerated)
{
    PresentJob job{};
    {
        std::unique_lock lock(m_mu);
        for (;;)
        {
            if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
                return false;
            if (m_qCount > 0)
            {
                job = m_q[m_qHead];
                m_qHead = (m_qHead + 1) % kQueueCap;
                --m_qCount;
                break;
            }
            m_cv.wait_for(lock, std::chrono::milliseconds(2));
            if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
                return false;
        }
    }

    // Pace to due time
    for (;;)
    {
        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
            return false;

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        double remainMs = double(job.dueQpc - now.QuadPart) * 1000.0 / double(freq.QuadPart);
        if (remainMs <= 0.05)
            break;

        double slice = std::min(remainMs, 2.0);
        if (hTimer)
        {
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>(slice * 10000.0);
            SetWaitableTimerEx(hTimer, &due, 0, nullptr, nullptr, nullptr, 0);
            HANDLE w[] = { hTimer, stopEvent };
            DWORD wr = WaitForMultipleObjects(2, w, FALSE, (DWORD)(slice + 2.0));
            if (wr == WAIT_OBJECT_0 + 1) return false;
        }
        else if (WaitForSingleObject(stopEvent, (DWORD)std::max(1.0, slice)) == WAIT_OBJECT_0)
            return false;
    }

    if (outGenerated) *outGenerated = job.isGenerated;
    if (job.slot < 0 || job.slot >= kPresentRing) return true;

    // PresentSrv takes ctx mutex internally
    return gfx.PresentSrv(m_srv[job.slot].get(), upscale, sharpness);
}

} // namespace df
