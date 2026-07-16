// Light spatial regularize of flow; preserves high-confidence vectors.
Texture2D<float4> FlowIn : register(t0);
RWTexture2D<float4> FlowOut : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    uint w, h;
    FlowIn.GetDimensions(w, h);
    if (tid.x >= w || tid.y >= h) return;

    int2 p = int2(tid.xy);
    float4 c = FlowIn[p];
    if (c.z < 0.4)
    {
        FlowOut[p] = float4(0, 0, c.z, 1);
        return;
    }

    float2 acc = c.xy * c.z * 2.0;
    float wsum = c.z * 2.0;

    int2 o[4] = { int2(1,0), int2(-1,0), int2(0,1), int2(0,-1) };
    [unroll] for (int i = 0; i < 4; ++i)
    {
        int2 q = clamp(p + o[i], int2(0, 0), int2(w, h) - 1);
        float4 n = FlowIn[q];
        float wt = n.z * exp(-length(n.xy - c.xy) * 0.2);
        acc += n.xy * wt;
        wsum += wt;
    }

    float2 f = (wsum > 1e-4) ? acc / wsum : c.xy;
    FlowOut[p] = float4(f, c.z, 1);
}
