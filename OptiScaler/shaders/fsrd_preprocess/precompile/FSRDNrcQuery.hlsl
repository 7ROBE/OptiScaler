// FSRD NRC query generation - packs NrcQuery structs for FidelityFX Neural Radiance Cache.
//
// Inputs are the already-converted FSRD textures from InputConv:
//   InNormals    : RG=octa normal, B=roughness, A=materialID
//   InDiffAlbedo : linear diffuse albedo
//   OutViewPos   : view-space position (from View Center Pos debug path) - we recompute from depth instead
//
// We generate one query per pixel of a quarter-res dispatch. Position is reconstructed
// in WORLD space via InvViewMatrix; view dir is the normalized view-space position.

#define FLAGS_LINEAR_DEPTH (1 << 0)
#define FLAGS_RIGHT_HANDED (1 << 1)
#define FLAGS_PACKED_ROUGHNESS (1 << 2)

struct NrcQuery
{
    float3 position;
    float2 octNormal;      // world->octahedral encoded here? InputConv normals are already octa (view or world space)
    float2 viewDir;
    float3 diffuseAlbedo;
    float roughness;
};

Texture2D<float4> InNormals : register(t0);
Texture2D<float4> InAlbedo : register(t1);
Texture2D<float> InDepth : register(t2);


RWStructuredBuffer<NrcQuery> OutQueries : register(u0);

cbuffer CB : register(b0)
{
    float4x4 InvViewMatrix;
    float4x4 InvProjMatrix;
    float NearPlane;
    float FarPlane;
    uint Flags;
    uint QueryCount;
    uint DstWidth;
    uint DstHeight;
};

float2 SignNotZero(float2 v)
{
    return float2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
}

bool IsSet(uint flag)
{
    return (Flags & flag) != 0;
}

float3 OctToNormal(float2 e)
{
    e = e * 2.0f - 1.0f; // [0,1] -> [-1,1]
    float3 n = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
    if (n.z < 0.0f)
        n.xy = (1.0f - abs(n.yx)) * SignNotZero(n.xy);
    return normalize(n);
}

float3 ViewPosFromDepth(uint2 px, float depth)
{
    const float2 uv = (float2(px) + 0.5f) * rcp(float2(DstWidth, DstHeight));
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // UV origin top-left -> NDC bottom-left

    float4 clip = float4(ndc, depth, 1.0f);
    float4 view = mul(InvProjMatrix, clip);
    return view.xyz / view.w;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint qidx = dtid.y * (DstWidth / 2) + dtid.x;
    if (qidx >= QueryCount)
        return;

    // Quarter-res pixel -> full-res source coordinate
    const uint2 srcPx = dtid.xy * 2;

    const float depth = InDepth[srcPx];
    NrcQuery q = (NrcQuery)0;

    if (depth <= 0.0f || depth >= 1.0f)
    {
        // Skybox/invalid: zero position marks the query invalid for NRC training
        q.position = float3(-1e8, -1e8, -1e8);
        OutQueries[qidx] = q;
        return;
    }

    const float3 viewPos = ViewPosFromDepth(srcPx, depth);
    q.position = mul(InvViewMatrix, float4(viewPos, 1.0f)).xyz;

    const float4 nrm = InNormals[srcPx];
    q.octNormal = nrm.rg;
    q.viewDir = normalize(viewPos).xy; // coarse view dir (octa-style xy)

    const float4 alb = InAlbedo[srcPx];
    q.diffuseAlbedo = alb.rgb;
    q.roughness = IsSet(FLAGS_PACKED_ROUGHNESS) ? nrm.b : alb.a;

    OutQueries[qidx] = q;
}


