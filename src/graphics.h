#pragma once
#include "common.h"
#include <mutex>

namespace df {

// Enumerate DXGI adapters (Iris Xe, RTX 3050 Ti, etc.)
std::vector<GpuInfo> EnumerateGpus();
// Prefer RTX 3050 Ti / discrete NVIDIA; fall back to best available.
int FindPreferredGpuIndex(const std::vector<GpuInfo>& gpus);

class Graphics
{
public:
    // adapterIndex = DXGI ordinal from EnumerateGpus(). -1 = auto preferred.
    bool Init(int adapterIndex = -1);
    // Tear down device and rebuild on another GPU (must not be scaling).
    bool RecreateDevice(int adapterIndex);
    void Shutdown();

    ID3D11Device*        Device()  const { return m_device.get(); }
    ID3D11DeviceContext* Context() const { return m_ctx.get(); }

    std::mutex& CtxMutex() { return m_ctxMutex; }

    int  ActiveAdapterIndex() const { return m_adapterIndex; }
    const std::wstring& ActiveAdapterName() const { return m_adapterName; }

    bool CreateOverlayTarget(HWND overlayHwnd, UINT width, UINT height);
    void ResizeOverlay(UINT width, UINT height);
    void DestroyOverlayTarget();

    bool PresentSrv(ID3D11ShaderResourceView* srv, bool useUpscale, float sharpness);
    bool PresentTexture(ID3D11Texture2D* tex, bool useUpscale, float sharpness);

    bool LoadShaders(const std::wstring& baseDir);

    ID3D11ComputeShader* CSLuma()       const { return m_csLuma.get(); }
    ID3D11ComputeShader* CSLumaScaled() const { return m_csLumaScaled.get(); }
    ID3D11ComputeShader* CSLumaDown()   const { return m_csLumaDown.get(); }
    ID3D11ComputeShader* CSFlow()       const { return m_csFlow.get(); }
    ID3D11ComputeShader* CSFlowFilter() const { return m_csFlowFilter.get(); }
    ID3D11ComputeShader* CSInterp()     const { return m_csInterp.get(); }
    ID3D11ComputeShader* CSUpscale()    const { return m_csUpscale.get(); }
    ID3D11ComputeShader* CSBlit()       const { return m_csBlit.get(); }
    ID3D11SamplerState*  LinearSampler() const { return m_linear.get(); }
    ID3D11Buffer*        CB()           const { return m_cb.get(); }

    bool HasOverlay() const { return m_swap != nullptr; }
    UINT OverlayW() const { return m_overlayW; }
    UINT OverlayH() const { return m_overlayH; }

    winrt::com_ptr<ID3D11Texture2D> CreateTex2D(
        UINT w, UINT h, DXGI_FORMAT fmt, UINT bindFlags);

    static std::wstring ExeDirectory();

private:
    bool CreateDevice(int adapterIndex);
    void ReleaseDeviceResources();
    bool LoadOneShader(const std::wstring& csoPath, const std::wstring& hlslPath,
                       const char* entry, winrt::com_ptr<ID3D11ComputeShader>& out);
    void UnbindCS();
    bool BlitToCurrentBackbuffer(ID3D11ShaderResourceView* srcSrv, bool upscale, float sharpness);

    std::mutex m_ctxMutex;

    int          m_adapterIndex = 0;
    std::wstring m_adapterName;

    winrt::com_ptr<ID3D11Device>        m_device;
    winrt::com_ptr<ID3D11DeviceContext> m_ctx;
    winrt::com_ptr<IDXGIFactory2>       m_factory;

    winrt::com_ptr<IDXGISwapChain1>       m_swap;
    winrt::com_ptr<IDCompositionDevice>   m_dcomp;
    winrt::com_ptr<IDCompositionTarget>   m_dcompTarget;
    winrt::com_ptr<IDCompositionVisual>   m_rootVisual;
    HWND m_overlayHwnd = nullptr;
    UINT m_overlayW = 0, m_overlayH = 0;
    UINT m_bufferCount = 3;

    winrt::com_ptr<ID3D11Texture2D>           m_presentStaging;
    winrt::com_ptr<ID3D11UnorderedAccessView> m_presentUav;
    winrt::com_ptr<ID3D11ShaderResourceView>  m_presentSrv;

    winrt::com_ptr<ID3D11ComputeShader> m_csLuma, m_csLumaScaled, m_csLumaDown;
    winrt::com_ptr<ID3D11ComputeShader> m_csFlow, m_csFlowFilter;
    winrt::com_ptr<ID3D11ComputeShader> m_csInterp, m_csUpscale, m_csBlit;
    winrt::com_ptr<ID3D11SamplerState>  m_linear;
    winrt::com_ptr<ID3D11Buffer>        m_cb;
};

} // namespace df
