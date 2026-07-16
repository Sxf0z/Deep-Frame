// Lightweight edge-adaptive upscale + sharpen (FSR-inspired).
Texture2D<unorm float4> InputTex : register(t0);
RWTexture2D<unorm float4> OutUAV : register(u0);
SamplerState LinearS : register(s0);

cbuffer Params : register(b0)
{
    float2 InvOutSize;
    float2 InvInSize;
    float Sharpness;
    float Pad[3];
};

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    uint ow, oh;
    OutUAV.GetDimensions(ow, oh);
    if (tid.x >= ow || tid.y >= oh) return;

    float2 uv = (float2(tid.xy) + 0.5) * InvOutSize;
    float2 oU = float2(InvInSize.x, 0);
    float2 oV = float2(0, InvInSize.y);

    float3 c = InputTex.SampleLevel(LinearS, uv, 0).rgb;
    float3 n = InputTex.SampleLevel(LinearS, uv - oV, 0).rgb;
    float3 s = InputTex.SampleLevel(LinearS, uv + oV, 0).rgb;
    float3 w = InputTex.SampleLevel(LinearS, uv - oU, 0).rgb;
    float3 e = InputTex.SampleLevel(LinearS, uv + oU, 0).rgb;

    float lC = Luma(c), lN = Luma(n), lS = Luma(s), lW = Luma(w), lE = Luma(e);
    float gH = abs(lE - lC) + abs(lC - lW);
    float gV = abs(lN - lC) + abs(lC - lS);
    float wH = saturate(1.0 - gH / (gV + 1e-5));
    float wV = saturate(1.0 - gV / (gH + 1e-5));
    float3 blend = lerp(c, lerp((w + e) * 0.5, (n + s) * 0.5, wV), wH * wV * 0.35);

    float contrast = max(lC, max(max(lN, lS), max(lW, lE))) - min(lC, min(min(lN, lS), min(lW, lE)));
    float amt = (0.15 + 0.35 * saturate(contrast * 2.0)) * Sharpness;
    float3 lap = (n + s + w + e) * 0.25;
    OutUAV[tid.xy] = float4(saturate(blend + (blend - lap) * amt), 1.0);
}
