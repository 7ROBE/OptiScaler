import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/precompile/FSRDNrcQuery.hlsl"
c = io.open(p, encoding="utf-8").read()

# Per official docs (gpuopen radiance-cache):
# - position: normalized to scene bounding box [0,1]^3 - we don't have the bbox; use a heuristic:
#   normalize by a fixed scene scale constant exposed via cbuffer (SceneScale).
#   v1: divide world pos by a large scale (e.g. 1000) -> approximately [0,1] for typical scenes.
# - normal/viewDir: POLAR form (r,phi), not octahedral.
#   r = acos(z) in [0,pi], phi = atan2(y,x) in [-pi,pi]
# - Counters: buffer of 2 uints [inferenceCount, trainingCount]. The packing shader must write
#   the actual sample counts so NRC knows buffer occupancy. We fill exactly QueryCount samples.

new_body = """// FSRD NRC query generation - fills the NRC prediction/training query buffers.
//
// Encodings per GPUOpen Radiance Cache docs (v0.9.0):
//   position : float3, normalized to scene bounding box (we approximate with SceneScale)
//   normal   : float2, polar form (r=acos(z), phi=atan2(y,x))
//   viewDir  : float2, polar form
//   albedo   : float3 RGB linear
//   roughness: scalar
//
// Dispatched at quarter res: each thread writes one query and we set the inference
// counter to QueryCount so NRC knows how many slots are valid.

#define FLAGS_PACKED_ROUGHNESS (1 << 2)

Texture2D<float4> InNormals : register(t0);   // RG=octa normal, B=roughness, A=material
Texture2D<float4> InAlbedo : register(t1);    // linear diffuse albedo
Texture2D<float> InDepth : register(t2);      // HW depth

RWBuffer<uint> OutQueries : register(u0);
RWBuffer<uint> OutCounters : register(u1);    // [0]=inference count, [1]=training count

cbuffer CB : register(b0)
{
    float4x4 InvViewMatrix;
    float4x4 InvProjMatrix;
    float NearPlane;
    float FarPlane;
    uint Flags;
    uint QueryCount;
    uint SrcWidth;
    uint SrcHeight;
    float SceneScale;   // world units corresponding to normalized 1.0
};

bool IsSet(uint flag)
{
    return (Flags & flag) != 0;
}

float3 OctToNormalVS(float2 e)
{
    e = e * 2.0f - 1.0f;
    float3 n = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
    if (n.z < 0.0f)
        n.xy = (1.0f - abs(n.yx)) * SignNotZero(n.xy);
    return normalize(n);
}

float2 SignNotZero(float2 v)
{
    return float2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
}

float2 ToPolar(float3 v)
{
    // Polar form per docs: (r, phi). r = acos(z)/pi normalized to [0,1],
    // phi = atan2(y,x) normalized to [0,1].
    const float r = acos(clamp(v.z, -1.0f, 1.0f)) / 3.14159265f;
    float phi = atan2(v.y, v.x);           // [-pi, pi]
    phi = phi < 0.0f ? phi + 6.2831853f : phi;  // [0, 2pi]
    return float2(r, phi / 6.2831853f);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint qidx = dtid.y * (SrcWidth / 2) + dtid.x;

    // Thread 0 publishes the valid-sample count for NRC's occupancy check
    if (qidx == 0u)
    {
        OutCounters[0] = QueryCount;  // inference samples
        OutCounters[1] = QueryCount;  // training samples
    }

    const uint2 srcPx = dtid.xy * 2;
    const float hwDepth = InDepth[srcPx];
    const uint base = qidx * 11u;

    if (hwDepth <= 0.0f || hwDepth >= 1.0f)
    {
        // Invalid pixel: zero the slot (position 0 = origin; NRC treats zero-albedo
        // queries as no-ops since remodulation multiplies by albedo).
        for (uint z = 0u; z < 11u; z++)
            OutQueries[base + z] = 0u;
        return;
    }

    const float2 uv = (float2(srcPx) + 0.5f) * rcp(float2(SrcWidth, SrcHeight));
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 clip = float4(ndc, hwDepth, 1.0f);
    float4 view = mul(InvProjMatrix, clip);
    const float3 viewPos = view.xyz / view.w;

    const float3 worldPos = mul(InvViewMatrix, float4(viewPos, 1.0f)).xyz;
    const float3 posNorm = saturate(worldPos / SceneScale + 0.5f); // approx scene-normalized

    const float4 nrm = InNormals[srcPx];
    const float roughness = IsSet(FLAGS_PACKED_ROUGHNESS) ? nrm.b : 1.0f;
    const float3 nrmVS = OctToNormalVS(nrm.rg);
    const float3 nrmWS = normalize(mul(nrmVS, (float3x3) InvViewMatrix));

    const float3 viewDir = normalize(viewPos);

    const float4 alb = InAlbedo[srcPx];

    const float2 nPolar = ToPolar(nrmWS);
    const float2 dPolar = ToPolar(viewDir);

    OutQueries[base + 0u] = asuint(posNorm.x);
    OutQueries[base + 1u] = asuint(posNorm.y);
    OutQueries[base + 2u] = asuint(posNorm.z);
    OutQueries[base + 3u] = asuint(nPolar.x);
    OutQueries[base + 4u] = asuint(nPolar.y);
    OutQueries[base + 5u] = asuint(dPolar.x);
    OutQueries[base + 6u] = asuint(dPolar.y);
    OutQueries[base + 7u] = asuint(alb.x);
    OutQueries[base + 8u] = asuint(alb.y);
    OutQueries[base + 9u] = asuint(alb.z);
    OutQueries[base + 10u] = asuint(roughness);
}
"""

io.open(p, "w", encoding="utf-8").write(c if False else new_body)
print("shader rewritten to doc spec")
