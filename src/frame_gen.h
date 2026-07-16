#pragma once
#include "common.h"
#include "graphics.h"

namespace df {

class FrameGenPipeline
{
public:
    explicit FrameGenPipeline(Graphics& gfx);

    bool EnsureSize(UINT width, UINT height, float flowScale);
    void Release();

    void BeginFrame();
    void Process(bool frameGen, bool performanceMode, int multiplier);

    ID3D11Texture2D* ColorCurr() const { return m_colorCurr.get(); }
    ID3D11Texture2D* ColorPrev() const { return m_colorPrev.get(); }
    ID3D11Texture2D* ColorInterp(int index = 0) const;
    ID3D11ShaderResourceView* SrvCurr() const { return m_srvColorCurr.get(); }
    ID3D11ShaderResourceView* SrvInterp(int index = 0) const;

    UINT Width()  const { return m_w; }
    UINT Height() const { return m_h; }
    bool Ready()  const { return m_colorCurr != nullptr; }
    bool HasHistory() const { return m_hasHistory; }
    int  GeneratedCount() const { return m_genCount; }

private:
    Graphics& m_gfx;
    UINT m_w = 0, m_h = 0;
    UINT m_fw = 0, m_fh = 0;
    float m_flowScale = 0.5f;
    float m_fullToFlow = 2.0f;
    bool m_hasHistory = false;
    int  m_genCount = 0;

    winrt::com_ptr<ID3D11Texture2D> m_colorCurr, m_colorPrev;
    winrt::com_ptr<ID3D11Texture2D> m_interp[3];
    winrt::com_ptr<ID3D11Texture2D> m_lumaFlowCurr, m_lumaFlowPrev;
    winrt::com_ptr<ID3D11Texture2D> m_flowA, m_flowB, m_flowTemporal;

    winrt::com_ptr<ID3D11ShaderResourceView> m_srvColorCurr, m_srvColorPrev;
    winrt::com_ptr<ID3D11ShaderResourceView> m_srvInterp[3];
    winrt::com_ptr<ID3D11ShaderResourceView> m_srvLumaFlowCurr, m_srvLumaFlowPrev;
    winrt::com_ptr<ID3D11ShaderResourceView> m_srvFlowA, m_srvFlowB, m_srvFlowTemporal;

    winrt::com_ptr<ID3D11UnorderedAccessView> m_uavLumaFlowCurr;
    winrt::com_ptr<ID3D11UnorderedAccessView> m_uavFlowA, m_uavFlowB;
    winrt::com_ptr<ID3D11UnorderedAccessView> m_uavInterp[3];

    void Unbind();
    void DispatchFlow(bool performanceMode);
    void DispatchInterpolate(float t, int outIndex);
};

} // namespace df
