// Single-pass: sample full-res RGB into flow-resolution luma (skips full-res R8 pass).
Texture2D<unorm float4> InputRGB : register(t0);
RWTexture2D<unorm float> OutLuma : register(u0);
SamplerState LinearS : register(s0);

cbuffer Params : register(b0)
{
    float2 InvFlowSize; // 1/fw, 1/fh — UV for flow pixel center maps to full image
    float2 Pad;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    uint fw, fh;
    OutLuma.GetDimensions(fw, fh);
    if (tid.x >= fw || tid.y >= fh) return;

    float2 uv = (float2(tid.xy) + 0.5) * InvFlowSize;
    float3 c = InputRGB.SampleLevel(LinearS, uv, 0).rgb;
    OutLuma[tid.xy] = dot(c, float3(0.299, 0.587, 0.114));
}
