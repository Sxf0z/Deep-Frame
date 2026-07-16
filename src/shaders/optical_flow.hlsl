// Dense-ish block matching at flow resolution.
// Output: R=flowX, G=flowY (FULL-RES pixels), B=confidence 0..1, A=unused
// Flow maps prev → curr (where a curr pixel came from in prev).

Texture2D<unorm float> LumaCurr : register(t0);
Texture2D<unorm float> LumaPrev : register(t1);
Texture2D<float4>      FlowPrev : register(t2);
RWTexture2D<float4>    FlowOut  : register(u0);

cbuffer Params : register(b0)
{
    int   SearchRadius;
    int   Step;
    float FullToFlow;
    float TemporalAlpha;
    uint  UseTemporal;
    uint  Pad0;
    float2 Pad1;
};

float SampleL(Texture2D<unorm float> tex, int2 p, int2 dims)
{
    p = clamp(p, int2(0, 0), dims - 1);
    return tex[p];
}

// 5-tap cross SAD — cheaper than 3x3, stable enough
float PatchSAD(int2 pos, int2 d, int2 dims)
{
    float sad = 0;
    sad += abs(SampleL(LumaCurr, pos, dims) - SampleL(LumaPrev, pos + d, dims));
    sad += abs(SampleL(LumaCurr, pos + int2(1, 0), dims) - SampleL(LumaPrev, pos + d + int2(1, 0), dims));
    sad += abs(SampleL(LumaCurr, pos + int2(-1, 0), dims) - SampleL(LumaPrev, pos + d + int2(-1, 0), dims));
    sad += abs(SampleL(LumaCurr, pos + int2(0, 1), dims) - SampleL(LumaPrev, pos + d + int2(0, 1), dims));
    sad += abs(SampleL(LumaCurr, pos + int2(0, -1), dims) - SampleL(LumaPrev, pos + d + int2(0, -1), dims));
    return sad;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    uint fw, fh;
    FlowOut.GetDimensions(fw, fh);
    if (tid.x >= fw || tid.y >= fh) return;

    uint lw, lh;
    LumaCurr.GetDimensions(lw, lh);
    int2 dims = int2((int)lw, (int)lh);
    int2 pos = int2(tid.xy);

    int2 seed = int2(0, 0);
    if (UseTemporal != 0)
    {
        float4 pf = FlowPrev[pos];
        seed = int2(round(pf.xy / max(FullToFlow, 1.0)));
        int r = SearchRadius;
        seed = clamp(seed, int2(-r, -r), int2(r, r));
    }

    float best = 1e9;
    int2 bestD = seed;
    int rad = SearchRadius;
    int st = max(Step, 1);

    for (int dy = -rad; dy <= rad; dy += st)
    for (int dx = -rad; dx <= rad; dx += st)
    {
        int2 d = seed + int2(dx, dy);
        // Prefer small vectors slightly (stability / less shimmer)
        float s = PatchSAD(pos, d, dims) + 0.008 * float(abs(d.x) + abs(d.y));
        if (s < best) { best = s; bestD = d; }
    }

    // ±1 refine
    [unroll] for (int ry = -1; ry <= 1; ++ry)
    [unroll] for (int rx = -1; rx <= 1; ++rx)
    {
        if (rx == 0 && ry == 0) continue;
        int2 d = bestD + int2(rx, ry);
        float s = PatchSAD(pos, d, dims) + 0.008 * float(abs(d.x) + abs(d.y));
        if (s < best) { best = s; bestD = d; }
    }

    float2 flowFull = float2(bestD) * FullToFlow;

    // 5 samples × max abs 1 → sad in ~0..5. Threshold ~1.2 is strict (sharp FG).
    float conf = saturate(1.0 - best / 1.25);

    if (UseTemporal != 0 && TemporalAlpha > 0.0)
    {
        float2 prev = FlowPrev[pos].xy;
        flowFull = lerp(flowFull, prev, TemporalAlpha * (1.0 - conf));
    }

    // Zero out untrusted motion entirely (prevents blurry wrong warps)
    if (conf < 0.35)
        flowFull = float2(0, 0);

    FlowOut[pos] = float4(flowFull, conf, 1.0);
}
