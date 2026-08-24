// FSR-RR Conversion & Packing Shader
#include "FSRDPreprocessCommon.hlsli"

#define MainRS \
    "RootFlags(0), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, numDescriptors = 11), visibility = SHADER_VISIBILITY_ALL), " \
    "DescriptorTable(UAV(u0, numDescriptors = 7), visibility = SHADER_VISIBILITY_ALL), "

// Dispatch config
#define THREAD_GROUP_SIZE_X     8
#define THREAD_GROUP_SIZE_Y     8
#define NUM_THREADS             (THREAD_GROUP_SIZE_X * THREAD_GROUP_SIZE_Y)

static const uint2 s_ThreadGroupSize = uint2(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y);

// Flags
#define FLAGS_NON_GAMMA_ALBEDO          (1 << 0)

#define FLAGS_PACKED_ROUGHNESS          (1 << 2)
#define FLAGS_MODE_2_SIGNAL             (1 << 3)
#define FLAGS_RIGHT_HANDED              (1 << 4)
#define FLAGS_CAMERA_CUT                (1 << 5)

// Debug Flags
#define FLAGS_DEBUG                     (1 << 16)
#define FLAGS_DEBUG_MODE_MASK           (0xFF << 16)

// Inputs
#define FLAGS_DEBUG_IN_SPEC_HIT_DIST    (1 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_IN_MOTION           (2 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_IN_NORMALS          (3 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_IN_ROUGHNESS        (4 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_IN_DIFF_ALBEDO      (5 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_IN_SPEC_ALBEDO      (6 << 17 | FLAGS_DEBUG)

// Outputs
#define FLAGS_DEBUG_OUT_FUSED_ALBEDO    (7 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_OUT_LINEAR_DEPTH    (8 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_OUT_MOTION          (9 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_OUT_NORMALS         (10 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_OUT_SPEC_ALBEDO     (11 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_OUT_DIFF_ALBEDO     (12 << 17 | FLAGS_DEBUG)

#define FLAGS_DEBUG_OUT_DEPTH_DELTA     (13 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_NORM_DEPTH          (14 << 17 | FLAGS_DEBUG)

#define FLAGS_DEBUG_ALBEDO_OVERSHOOT    (15 << 17 | FLAGS_DEBUG)

#define FLAGS_DEBUG_FLOOR_VARIANCE      (16 << 17 | FLAGS_DEBUG)
#define FLAGS_DEBUG_FLOOR_COLOR         (17 << 17 | FLAGS_DEBUG)

// DLSS-RR Inputs
Texture2D<half3> InColor : register(t0); // RGB - NVSDK_NGX_Parameter_Color
Texture2D<float> InDepth : register(t1); // R - NVSDK_NGX_Parameter_Depth - hardware or linear - inverted or not
Texture2D<float3> InMotionVectors : register(t2); // RG - NVSDK_NGX_Parameter_MotionVectors
Texture2D<float4> InNormals : register(t3); // RGB: Normals, A: Roughness (Optional) - NVSDK_NGX_Parameter_GBuffer_Normals
Texture2D<float> InRoughness : register(t4); // R - May be packed in normals. NVSDK_NGX_Parameter_GBuffer_Roughness
Texture2D<float> InSpecHitDist : register(t5); // R - NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance
Texture2D<half3> InDiffAlbedo : register(t6); // RGB - NVSDK_NGX_Parameter_GBuffer_DiffuseAlbedo
Texture2D<half3> InSpecAlbedo : register(t7); // RGB - NVSDK_NGX_Parameter_GBuffer_SpecularAlbedo
Texture2D<half> InBiasMask : register(t8);

Texture2D<half4> InFloorColor : register(t9);

// Previous frame's floor (skip signal). Used to temporally stabilize the floor -
// the per-frame spatial median of noisy input still carries noise which boils
// when added back on top of the denoised output. Blending with the previous
// floor (gated by similarity) removes that temporal noise.
Texture2D<half4> InPrevFloorColor : register(t10);

// FSR-RR - ffxDispatchDescDenoiserInput1Signal or ffxDispatchDescDenoiserInput2Signals
//
// Mode 1: RGB: Noisy fused lighting
// Mode 2: RGB: Noisy specular lighting A: Specular Ray Length
RWTexture2D<half4> OutSignal1 : register(u0); 

// Mode 1: RGB Fused Albedo: max(specularAlbedo, diffuseAlbedo)
// Mode 2: RGB: Noisy diffuse lighting for Mode 2
RWTexture2D<half4> OutSignal2 : register(u1);

// ffxDispatchDescDenoiser
RWTexture2D<half4> OutMotion : register(u2); // RG: Standard TSR motion vectors, B: Signed Linear Depth Delta (PrevLinearDepth - CurrentLinearDepth)
RWTexture2D<half4> OutNormals : register(u3); // RG: Octahedrally encoded normals, B: Linear Roughness, A: Material Type (Optional)
RWTexture2D<half4> OutSpecAlbedo : register(u4); // RGB: Specular Albedo, A: dot(Normal, ViewDir)
RWTexture2D<half4> OutDiffAlbedo : register(u5); // RGB: Diffuse Albedo, A: Metalness (not provided)

RWTexture2D<half4> OutSkipSignal : register(u6);

cbuffer CB_Packing : register(b0)
{
    float4x4 InvViewMatrix; // DLSSD WorldToView^-1
    float4x4 InvProjMatrix; // DLSSD ViewToClip^-1
    float4x4 PrevViewMatrix; // DLSSD WorldToView from last frame
    
    float4 DstTexSize; // Resolution of inputs
    
    float NearPlane;
    float FarPlane;   
    
    float FloorIsolation;
    uint Flags;
    
    float4x4 ProjMatrix;     // ViewToClip (current)
    float4x4 PrevProjMatrix; // ViewToClip (previous) - for reflection-space reprojection
};

bool IsSet(uint mask) { return (Flags & mask) == mask; }
uint GetDebugMode() { return (Flags & FLAGS_DEBUG_MODE_MASK); }

float3 GetViewSpacePos(const int2 px)
{
    const float inDepth = abs(InDepth[px]);
    const float2 uv = (float2(px) + 0.5) * DstTexSize.zw;
    float3 viewSpacePos = 0.0f;
    
    // RR 1.2.0: linear depth is signed - the sign follows the view space facing direction.
    const float depthSign = IsSet(FLAGS_RIGHT_HANDED) ? -1.0f : 1.0f;

    viewSpacePos = InvProjectPosition(float3(uv, 1.0f), InvProjMatrix);
    viewSpacePos.xy *= abs(inDepth / viewSpacePos.z);
    viewSpacePos.z = depthSign * inDepth;

    return viewSpacePos;
}

// Main Kernel
//
[RootSignature(MainRS)]
[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CSMain(uint3 groupID : SV_GroupID, uint3 gtID : SV_GroupThreadID)
{
    const uint2 px = groupID.xy * s_ThreadGroupSize + gtID.xy;
    
    if (px.x >= DstTexSize.x || px.y >= DstTexSize.y)
        return;

    // Albedo / reflectance
    //
    // Zeroed albedos are unusable sentinels and must be skipped.
    // Depth values at the far plane indicate a skybox or other skippable content.
    //
    // DLSS-RR specular albedo is hemispherical specular reflectance at (NoV, roughness).
    // Diffuse albedo is the diffuse component of reflectance.     
    float3 specReflectance = GetSafeFP16(InSpecAlbedo[px].rgb);
    float3 diffAlbedo = GetSafeFP16(InDiffAlbedo[px].rgb);
    
    const float totalAlbedo = dot(specReflectance.rgb + diffAlbedo.rgb, 1.0f);
    const float isEmissive = (totalAlbedo > 5.9f);   
    diffAlbedo.rgb *= (1.0f - isEmissive);
    specReflectance.rgb = lerp(specReflectance.rgb, 0.1f, isEmissive);
    
    // Clamp albedo
    const float3 albedoOvershoot = max((specReflectance.rgb + diffAlbedo.rgb) - 1.0f, 0.0f);
    specReflectance.rgb = saturate(specReflectance.rgb - albedoOvershoot);
    diffAlbedo.rgb -= max((specReflectance.rgb + diffAlbedo.rgb) - 1.0f, 0.0f);
    specReflectance.rgb = max(specReflectance.rgb, 1e-4f);
    diffAlbedo.rgb = max(diffAlbedo.rgb, 1e-4f);
    
    // Denoiser input color and floor residual
    const float3 rawColor = GetSafeFP16(InColor[px].rgb);
    float4 floorColor = InFloorColor[px];  

    // Motion vectors (pixel movement, current -> previous) + early reflection-space motion
    const float2 motionIn = InMotionVectors[px].rg;
    
    // Reflection-space motion: on smooth surfaces the reflected content moves with the virtual
    // hit point (P' = X + t*R), not the surface. Reconstruct it so the floor history reprojects
    // correctly inside reflections during camera pans. Mode 2 only (needs specular hit distance).
    float2 virtualMotion = 0.0f;
    float reflectionWeight = 0.0f;
    [branch]
    if (IsSet(FLAGS_MODE_2_SIGNAL) && !IsSet(FLAGS_DEBUG))
    {
        const float4 earlyNormal = InNormals[px];
        const float earlyRough = IsSet(FLAGS_PACKED_ROUGHNESS) ? earlyNormal.a : InRoughness[px];
        const float smoothness = saturate(1.0f - earlyRough * 5.0f);
        const float hitT = InSpecHitDist[px];
        
        [branch]
        if (hitT > 1e-3f && hitT < 1e4f && smoothness > 0.05f)
        {
            // Virtual image point: X + t*V (view dir), NOT the real hit point X + t*R.
            // The virtual point lies ON this pixel's view ray, so it projects back onto the
            // pixel itself (built-in sanity check), and its previous-frame projection gives
            // the correct screen-space motion of the reflected content for history reuse.
            // This matches FSR's internal Virtual Hit Pos and NRD's specular formulation.
            const float3 earlyViewPos = GetViewSpacePos(px);
            const float3 viewDir = normalize(earlyViewPos);
            const float3 hitViewPos = earlyViewPos + viewDir * hitT;
            const float3 hitWorldPos = mul(InvViewMatrix, float4(hitViewPos, 1.0f)).xyz;
            const float3 hitPrevViewPos = mul(PrevViewMatrix, float4(hitWorldPos, 1.0f)).xyz;
            
            // Current frame: virtual point is on the view ray -> projects to this pixel.
            // Use px directly (self-test: if uvCur ever deviates, inputs are broken).
            const float4 hitClipPrev = mul(PrevProjMatrix, float4(hitPrevViewPos, 1.0f));
            
            [branch]
            if (hitClipPrev.w > 1e-4f)
            {
                const float2 uvPrev = NDCToUV(hitClipPrev.xy / hitClipPrev.w);
                const float2 uvCur = (float2(px) + 0.5f) * DstTexSize.zw;
                virtualMotion = (uvPrev - uvCur) * DstTexSize.xy; // pixels
                reflectionWeight = smoothness;
            }
        }
    }
    
    // Reprojection vector for the floor history: surface motion on rough surfaces,
    // blended towards the virtual reflection motion on smooth ones.
    const float2 mv = lerp(motionIn, virtualMotion, reflectionWeight);

    // Temporal floor stabilization: the spatial median still carries per-frame noise.
    // Sample the previous floor at the REPROJECTED position - without reprojection the blend
    // compares unrelated pixels while moving and the gate collapses, bringing back boiling
    // exactly during motion. Off-screen or invalid samples fall back to the current floor.
    const float2 prevCoord = float2(px) + mv;
    const bool inBounds = all(prevCoord >= 0.0f) && prevCoord.x < DstTexSize.x - 1 && prevCoord.y < DstTexSize.y - 1;
    
    // Bilinear reprojected sample with validity rejection - skipped pixels store negative
    // alpha (invalid); blending raw color from them caused flashing artifacts.
    float4 reprojFloor = floorColor;
    if (inBounds)
    {
        const float2 f = frac(prevCoord);
        const int2 c = int2(floor(prevCoord));
        const int2 cp = min(c + int2(1, 1), int2(DstTexSize.xy) - 1);
        
        const float4 s00 = InPrevFloorColor[c];
        const float4 s10 = InPrevFloorColor[int2(cp.x, c.y)];
        const float4 s01 = InPrevFloorColor[int2(c.x, cp.y)];
        const float4 s11 = InPrevFloorColor[cp];
        
        // Zero out invalid taps (negative alpha) so they contribute nothing to the blend
        const float w00 = s00.a >= 0.0h ? (1.0f - f.x) * (1.0f - f.y) : 0.0f;
        const float w10 = s10.a >= 0.0h ? f.x * (1.0f - f.y) : 0.0f;
        const float w01 = s01.a >= 0.0h ? (1.0f - f.x) * f.y : 0.0f;
        const float w11 = s11.a >= 0.0h ? f.x * f.y : 0.0f;
        const float wSum = w00 + w10 + w01 + w11;
        
        if (wSum > 1e-4f)
        {
            reprojFloor = (s00 * w00 + s10 * w10 + s01 * w01 + s11 * w11) / wSum;
            reprojFloor.a = abs(reprojFloor.a); // bilinear of valid alphas is positive
        }
    }
    
    const float4 prevFloor = reprojFloor;
    const float floorTemporalSim = GetRelativeSimilarity(GetLuminance(floorColor.rgb), GetLuminance(prevFloor.rgb), 0.3f);
    
    // Velocity-adaptive history weight, quantized to reduce frame-to-frame pumping:
    // slow motion keeps maximum temporal stability, fast motion damps history to avoid ghost trails.
    const float mvLen = length(mv);
    const float velocityDamp = saturate(1.0f - mvLen / 12.0f);
    const float velStep = velocityDamp > 0.66f ? 1.0f : (velocityDamp > 0.33f ? 0.6f : 0.3f);
    // Camera cut: history is from a different scene - use current floor only this frame
    const float temporalWeight = floorTemporalSim * lerp(0.5f, 0.85f, velStep);
    floorColor.rgb = GetSafeFP16(lerp(floorColor.rgb, prevFloor.rgb, temporalWeight));
    
    const float rawLuma = GetLuminance(rawColor);
    const float floorLuma = GetLuminance(floorColor.rgb);
    floorColor.a = floorLuma;

    // Floor color blending
    //
    // Diffuse dominant surfaces are relatively well behaved.
    const float avgSpecular = dot(specReflectance.rgb, 0.33f);
    const float diffuseDominance = smoothstep(0.08f, 0.0f, avgSpecular);
    const float similarityThreshold = lerp(0.5f, 0.2f, diffuseDominance);
    
    // Clamp floor to minimum and blend in raw values where similar to preserve microcontrast.
    const float floorSimilarity = GetRelativeSimilarity(floorLuma, rawLuma, similarityThreshold);
    floorColor.rgb = FloorIsolation * lerp(floorColor.rgb, rawColor, saturate(floorSimilarity));
    floorColor.rgb = min(rawColor, floorColor.rgb);
    
    // Signal denoising pre-pass: compress fireflies/outliers in the residual BEFORE demodulation.
    // The demod division (color / albedo) amplifies any residual noise by 1/albedo - a single bright
    // spike on a dark albedo becomes a huge signal outlier that survives FFX-RR's filtering and shows
    // up as NN INPUT1/2 salt-and-pepper. Soft-knee the residual against the smoothed floor luma:
    // deviations within the noise envelope pass, large spikes are compressed toward it.
    float3 signalColor = rawColor - floorColor.rgb;
    {
        const float residualLuma = GetLuminance(signalColor);
        const float floorRef = max(floorLuma, 1e-3f);
        const float spikeLimit = floorRef * 2.0f + 0.05f; // noise envelope scales with local brightness
        
        if (residualLuma > spikeLimit)
        {
            const float compressed = spikeLimit + (residualLuma - spikeLimit) * 0.15f;
            signalColor *= compressed / residualLuma; // preserve chroma ratio, scale magnitude
        }
    }
    const float3 denoiserColor = max(signalColor, 0.0f);

    // Depth - full position needed for reprojected depth delta
    const float3 viewSpacePos = GetViewSpacePos(px);
    const float compressedDepth = log(abs(viewSpacePos.z) + 1.0f) / log(FarPlane + 1.0f);
    
    if (((compressedDepth < 0.99f) && totalAlbedo > 1e-2f) || IsSet(FLAGS_DEBUG))
    {        
        // Normals - FSR-RR requries world normals.
        //
        // [TODO!] DLSS-RR normals may be in view or world space. They will need to be transformed to account
        // for both configurations. Cyberpunk happens to use world normals, thankfully.
        float4 worldSurfaceNormal = InNormals[px];        
        const float2 octNormal = OctahedralEncode(worldSurfaceNormal.rgb);
        
        // Synthetic material IDs (FSR-RR normals.A, 0-3 normalized). RR rejects temporal mixing
        // between mismatched IDs - spending the three custom slots on the boundaries that cause
        // the worst cross-material ghosting:
        //   0 = default (dielectrics, rough surfaces)
        //   1 = metal / mirror-like - high or strongly-tinted F0. Dielectric F0 ~= 0.04 flat grey;
        //       metals are brighter and colored. Quantized with a wide dead-zone band so textured
        //       albedo doesn't fragment the ID per-texel (IDs must stay low-frequency and stable).
        //   2 = emissive - reuses the existing emissive detection; neon-on-dark is the worst case.
        // SSS/skin would need an engine guide buffer DLSS doesn't tag - left as default.
        float materialType = 0.0f;
        if (isEmissive > 0.5f)
            materialType = 2.0f / 3.0f;
        else
        {
            // Coarse metal classification: peak F0 with a soft band between 0.15 and 0.35 -
            // well above dielectric 0.04, tolerant of BRDF view-dependence.
            const float f0Peak = max(specReflectance.r, max(specReflectance.g, specReflectance.b));
            materialType = smoothstep(0.15f, 0.35f, f0Peak) * (1.0f / 3.0f);
        }
    
        // DLSS-RR provides 3D normals
        // Linear roughness optionally included in the A channel, or in a separate single-channel 
        // buffer (InRoughness).
        float roughness = IsSet(FLAGS_PACKED_ROUGHNESS) ? worldSurfaceNormal.a : InRoughness[px];
        roughness *= (1.0f - isEmissive);
        
        // Output: RG=OctNormal, B=Roughness, A=MaterialID
        OutNormals[px] = GetSafeFP16(float4(octNormal, roughness, materialType));
   
        // Motion Vectors & Depth Delta
        //
        // Find the current pixel in world space and calculate movement in view space
        const float3 worldSpacePos = mul(InvViewMatrix, float4(viewSpacePos, 1.0f)).xyz;
        float3 prevViewSpacePos = mul(PrevViewMatrix, float4(worldSpacePos, 1.0f)).xyz;
            
        // FSR-RR requires Linear Depth Delta in Blue channel
        const float depthDelta = (prevViewSpacePos.z - viewSpacePos.z);
        const float3 motionOut3 = float3(motionIn, depthDelta);
        OutMotion[px] = half4(motionOut3, 0.0f);

        half hitDist = hitDist = 0.0f;
        half3 demodColor = 0.0f;
        float3 fusedAlbedo = 0.0f;
        
        [branch]
        if (IsSet(FLAGS_MODE_2_SIGNAL)) // Primary radiance packing - Mode 2 Signal
        {          
            const float3 specWeight = saturate(specReflectance.rgb);
            const float3 diffWeight = saturate(diffAlbedo.rgb);
            const float3 rcpTotalWeight = rcp(diffWeight + specWeight);

            const float3 specularColor = denoiserColor * (specWeight * rcpTotalWeight);
            const float3 diffuseColor = denoiserColor - specularColor;

            half3 demodSpecular = GetSafeFP16(specularColor / specReflectance.rgb);         
            half3 demodDiffuse = GetSafeFP16(diffuseColor / diffAlbedo.rgb);

            // Anything that can't survive modulation and clamping should be skipped
            const float3 remodColor = (demodSpecular * specReflectance.rgb) + (demodDiffuse * diffAlbedo.rgb);
            const float3 residual = max(0.0f, denoiserColor - remodColor);           
            floorColor.rgb += residual;
            
            // Mask out specular tracking if the surface isn't smooth enough
            // Pass the real hit distance for all surfaces - zeroing it on rough/emissive pixels
            // tells the denoiser "immediate hit", which corrupts temporal accumulation and
            // blurs reflections. Emissive has no meaningful hit, keep a large sentinel instead.
            hitDist = isEmissive ? half(0.0f) : GetSafeFP16(max(InSpecHitDist[px], 1e-4f));
            
            [branch]
            if (!IsSet(FLAGS_DEBUG))
            {
                OutSignal1[px] = half4(demodSpecular, hitDist);
                OutSignal2[px] = half4(demodDiffuse, 0.0f);
            }
            else
                demodColor = demodDiffuse + demodSpecular;
        }
        else // Primary radiance packing - Mode 1 Signal
        {           
            fusedAlbedo = max(specReflectance.rgb, diffAlbedo.rgb);
            demodColor = GetSafeFP16(denoiserColor / fusedAlbedo.rgb);
            
            const float3 residual = max(0.0f, denoiserColor - (demodColor * fusedAlbedo.rgb));
            floorColor.rgb += residual;
            
            [branch]
            if (!IsSet(FLAGS_NON_GAMMA_ALBEDO))
                fusedAlbedo = sqrt(fusedAlbedo);
            
            [branch]
            if (!IsSet(FLAGS_DEBUG))
            {
                OutSignal1[px] = half4(demodColor, hitDist);
                OutSignal2[px] = half4(GetSafeFP16(fusedAlbedo), 0.0f);
            }
        }        

        // May be for better perceptual encoding efficiency in some configurations
        [branch]
        if (!IsSet(FLAGS_NON_GAMMA_ALBEDO))
        {
            specReflectance = sqrt(specReflectance);
            diffAlbedo = sqrt(diffAlbedo);
        }
        
        OutSpecAlbedo[px] = half4(GetSafeFP16(specReflectance), 0.0f);
        OutDiffAlbedo[px] = half4(GetSafeFP16(diffAlbedo), 0.0f);
        OutSkipSignal[px] = half4(GetSafeFP16(floorColor));
        
        [branch]
        if (IsSet(FLAGS_DEBUG))
        {
            float3 debugColor = float3(0, 0, 0);
        
            switch (GetDebugMode())
            {
                // Inputs
                case FLAGS_DEBUG_IN_SPEC_HIT_DIST:
                    debugColor = TurboColormap(frac(hitDist * 0.1f));
                    break;
                
                case FLAGS_DEBUG_NORM_DEPTH:
                    debugColor = TurboColormap(compressedDepth);
                    break;
                
                case FLAGS_DEBUG_IN_MOTION:
                    debugColor = VisualizeMotionVec(motionIn * DstTexSize.xy, 0.1f);
                    break;
                
                case FLAGS_DEBUG_IN_NORMALS:
                    debugColor = worldSurfaceNormal.rgb * 0.5 + 0.5;
                    break;
                
                case FLAGS_DEBUG_IN_ROUGHNESS:
                    debugColor = roughness;
                    break;
                
                case FLAGS_DEBUG_IN_DIFF_ALBEDO:
                    debugColor = InDiffAlbedo[px];
                    break;
                
                case FLAGS_DEBUG_IN_SPEC_ALBEDO:
                    debugColor = InSpecAlbedo[px];
                    break;
                // Outputs
                case FLAGS_DEBUG_OUT_FUSED_ALBEDO:
                    debugColor = fusedAlbedo.rgb;
                    break;
                
                case FLAGS_DEBUG_OUT_LINEAR_DEPTH:
                    debugColor = TurboColormap(frac(abs(viewSpacePos.z) * 0.1));
                    break;
                
                case FLAGS_DEBUG_OUT_MOTION:
                    debugColor = VisualizeMotionVec(motionIn * DstTexSize.xy, 0.1f);
                    break;

                case FLAGS_DEBUG_OUT_DEPTH_DELTA:
                    debugColor = VisualizeSignedDiff(depthDelta, 5.0f);
                    break;
                
                case FLAGS_DEBUG_OUT_NORMALS:
                    debugColor = OctahedralDecode(octNormal) * 0.5 + 0.5;
                    break;

                case FLAGS_DEBUG_OUT_SPEC_ALBEDO:
                    debugColor = specReflectance.rgb;
                    break;
                
                case FLAGS_DEBUG_OUT_DIFF_ALBEDO:
                    debugColor = diffAlbedo.rgb;
                    break;

                case FLAGS_DEBUG_FLOOR_VARIANCE:
                    debugColor = TurboColormap(InFloorColor[px].a);
                    break;
                
                case FLAGS_DEBUG_FLOOR_COLOR:
                    debugColor = InFloorColor[px].rgb;
                    break;

                case FLAGS_DEBUG_ALBEDO_OVERSHOOT:
                    debugColor = albedoOvershoot;
                    break;
                
                default:
                    debugColor = demodColor;
                    break;
            }
        
            OutSignal1[px] = half4(debugColor, 1.0f);
        }
    }
    else // Skip
    {
        OutNormals[px] = 0.0f;
        OutSpecAlbedo[px] = 0.0f;
        OutDiffAlbedo[px] = 0.0f;
        OutSignal1[px] = 0.0f;
        OutSignal2[px] = 0.0f;
        // Negative alpha marks this texel as INVALID for temporal floor reuse - the temporal
        // blend must not sample noisy raw color from skipped pixels (caused flashing).
        OutSkipSignal[px] = half4(rawColor, -1.0f);
    }
}