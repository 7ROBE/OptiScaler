// FSRD NRC query generation - fills the NRC prediction/training query buffers.
//
// Uses typed RWBuffer<uint> packing (44 bytes = 11 uints per query) so UAVs can be created
// through the standard typed-UAV path. Layout matches FFX NRC RadianceCacheInput:
//   position(3f) octaNormal(2f) octaViewDir(2f) diffuseAlbedo(3f) roughness(1f)
//
// Dispatched at quarter res: each thread covers a 2x2 pixel block.

#define FLAGS_PACKED_ROUGHNESS (1 << 2)

Texture2D<float4> InNormals : register(t0);   // RG=octa normal, B=roughness, A=material
Texture2D<float4> InAlbedo : register(t1);    // linear diffuse albedo
Texture2D<float> InDepth : register(t2);      // HW depth

RWBuffer<uint> OutQueries : register(u0);

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
};

float2 SignNotZero(float2 v)
{
    return float2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
}

bool IsSet(uint flag)
{
    return (Flags & flag) != 0;
}

float3 OctToNormalWS(float2 e)
{
    e = e * 2.0f - 1.0f;
    float3 n = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
    if (n.z < 0.0f)
        n.xy = (1.0f - abs(n.yx)) * SignNotZero(n.xy);
    return normalize(n);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // Quarter-res: each thread handles one query from a 2x2 source block
    const uint2 srcPx = dtid.xy * 2;

    const float hwDepth = InDepth[srcPx];

    if (hwDepth <= 0.0f || hwDepth >= 1.0f)
    {
        // Invalid: zero the whole query slot (position 0 marks unused for NRC training skip)
        const uint qidx = dtid.y * (SrcWidth / 2) + dtid.x;
        for (uint i = 0u; i < 11u; i++)
            OutQueries[qidx * 11u + i] = 0u;
        return;
    }

    // Reconstruct view-space position from HW depth via inverse projection.
    // NOTE: assumes non-linear HW depth; FLAGS check kept simple for v1.
    const float2 uv = (float2(srcPx) + 0.5f) * rcp(float2(SrcWidth, SrcHeight));
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 clip = float4(ndc, hwDepth, 1.0f);
    float4 view = mul(InvProjMatrix, clip);
    const float3 viewPos = view.xyz / view.w;

    const float3 worldPos = mul(InvViewMatrix, float4(viewPos, 1.0f)).xyz;

    const float4 nrm = InNormals[srcPx];
    const float roughness = IsSet(FLAGS_PACKED_ROUGHNESS) ? nrm.b : 1.0f;
    const float3 nrmVS = OctToNormalWS(nrm.rg);
    const float3 nrmWS = normalize(mul(nrmVS, (float3x3) InvViewMatrix));

    const float3 viewDir = normalize(viewPos); // view ray direction in view space

    const float4 alb = InAlbedo[srcPx];

    const uint qidx = dtid.y * (SrcWidth / 2) + dtid.x;
    const uint base = qidx * 11u;
    OutQueries[base + 0u] = asuint(worldPos.x);
    OutQueries[base + 1u] = asuint(worldPos.y);
    OutQueries[base + 2u] = asuint(worldPos.z);
    OutQueries[base + 3u] = asuint(nrmWS.x);
    OutQueries[base + 4u] = asuint(nrmWS.y);
    OutQueries[base + 5u] = asuint(viewDir.x);
    OutQueries[base + 6u] = asuint(viewDir.y);
    OutQueries[base + 7u] = asuint(alb.x);
    OutQueries[base + 8u] = asuint(alb.y);
    OutQueries[base + 9u] = asuint(alb.z);
    OutQueries[base + 10u] = asuint(roughness);
}
