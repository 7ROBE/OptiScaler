import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/precompile/FSRDInputConv.hlsl"
c = io.open(p, encoding="utf-8").read()

old = """            {
                const float snr = saturate(floorLuma * 20.0f);          // near-black => low SNR
                const float preDemodW = (1.0f - snr) * 0.5f;            // up to 50% floor in shadows
                denoiserColor = GetSafeFP16(lerp(denoiserColor, floorColor.rgb, half(preDemodW)));
            }

            const float3 specularColor = denoiserColor * (specWeight * rcpTotalWeight);
            const float3 diffuseColor = denoiserColor - specularColor;"""
new = """            }"""
# Remove the block from inside; re-add before the [branch] with a local var
c = c.replace(old, new, 1)

old2 = """        if (IsSet(FLAGS_MODE_2_SIGNAL)) // Primary radiance packing - Mode 2 Signal
        {  
            const float3 specWeight = saturate(specReflectance.rgb);"""
new2 = """        if (IsSet(FLAGS_MODE_2_SIGNAL)) // Primary radiance packing - Mode 2 Signal
        {  
            // PRE-DEMOD VARIANCE REDUCTION: blend raw radiance toward the temporally-stabilized
            // floor BEFORE splitting and before dividing by albedo.
            // Var(X/a) = Var(X)/a^2: variance reduction here is quadratically more effective
            // than any post-demod filter - attacks shadow boiling at its mathematical source.
            const float snrPre = saturate(floorLuma * 20.0f);       // near-black => low SNR
            const float preDemodW = (1.0f - snrPre) * 0.5f;         // up to 50% floor in shadows
            const float3 stabilizedRadiance = GetSafeFP16(lerp(denoiserColor, floorColor.rgb, half(preDemodW)));

            const float3 specWeight = saturate(specReflectance.rgb);"""
assert old2 in c
c = c.replace(old2, new2, 1)

# Use stabilizedRadiance for the split
old3 = """            const float3 specularColor = denoiserColor * (specWeight * rcpTotalWeight);
            const float3 diffuseColor = denoiserColor - specularColor;"""
new3 = """            const float3 specularColor = stabilizedRadiance * (specWeight * rcpTotalWeight);
            const float3 diffuseColor = stabilizedRadiance - specularColor;"""
assert old3 in c
c = c.replace(old3, new3, 1)

io.open(p, "w", encoding="utf-8").write(c)
print("restructured with local const")
