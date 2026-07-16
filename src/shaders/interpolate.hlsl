// SHARP frame generation — NO dual-frame blending (that was the motion blur).
//
// Mode 0 (mid between prev→curr): warp PREV forward by t * flow
// Mode 1 (forward from curr):     warp CURR forward by t * flow
//
// Low confidence → hold a single sharp frame (curr). Never crossfade.

Texture2D<unorm float4> ColorPrev : register(t0);
Texture2D<unorm float4> ColorCurr : register(t1);
Texture2D<float4>       FlowTex   : register(t2); // xy flow full-px, z conf
RWTexture2D<unorm float4> OutUAV  : register(u0);
SamplerState            LinearS   : register(s0);

cbuffer Params : register(b0)
{
    float2 InvFullSize;
    float2 InvFlowSize;
    float  Time;           // 0.5 for x2
    float  FlowScale;
    float  ConfThreshold;  // e.g. 0.4
    uint   Mode;           // 0 = mid (warp prev), 1 = forward (warp curr)
};

float4 SampleFlow(float2 fullPos)
{
    float2 uv = (fullPos + 0.5) * InvFullSize;
    return FlowTex.SampleLevel(LinearS, uv, 0);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    uint w, h;
    OutUAV.GetDimensions(w, h);
    if (tid.x >= w || tid.y >= h) return;

    float2 pos = float2(tid.xy);
    float t = Time;
    float4 f = SampleFlow(pos);
    float2 flow = f.xy;
    float conf = f.z;

    float2 uv = (pos + 0.5) * InvFullSize;
    float4 sharpCurr = ColorCurr.SampleLevel(LinearS, uv, 0);
    sharpCurr.a = 1.0;

    // Static / untrusted: keep sharp current (UI, text, low motion)
    if (conf < ConfThreshold || length(flow) < 0.25)
    {
        OutUAV[tid.xy] = sharpCurr;
        return;
    }

    float4 warped;
    if (Mode == 1)
    {
        // Extrapolate current along motion
        float2 sUV = saturate((pos - flow * t + 0.5) * InvFullSize);
        warped = ColorCurr.SampleLevel(LinearS, sUV, 0);
    }
    else
    {
        // Midpoint: take prev, push along half motion toward curr
        float2 sUV = saturate((pos - flow * t + 0.5) * InvFullSize);
        warped = ColorPrev.SampleLevel(LinearS, sUV, 0);
    }
    warped.a = 1.0;

    // Soft mix only at confidence edge — mostly hard select (anti-blur)
    float useWarp = smoothstep(ConfThreshold, ConfThreshold + 0.25, conf);
    // Reject warps that diverge too hard from curr (occlusion / bad MV)
    float diff = dot(abs(warped.rgb - sharpCurr.rgb), float3(0.333, 0.333, 0.333));
    if (diff > 0.22)
        useWarp *= 0.15; // almost pure curr — no ghost trail

    float4 outC = lerp(sharpCurr, warped, useWarp);
    outC.a = 1.0;
    OutUAV[tid.xy] = outC;
}
