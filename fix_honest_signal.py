import io

# THE REAL FIX per the corrected analysis. The residual/skip architecture is the blur engine.
# Restructure to honest native signal feeding (like the AMD sample):
#
# 1. InputConv: signal = full demodulated radiance (raw/albedo), NOT raw-floor.
#    Spike compression stays but generous (fireflies only).
# 2. SkipSignal = neutralized (NN output IS the complete lighting).
# 3. Floor pipeline becomes unused for the signal path (keep code, stop feeding it).
#
# Edit FSRDInputConv.hlsl:

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/precompile/FSRDInputConv.hlsl"
c = io.open(p, encoding="utf-8").read()

old = """// Feed the FULL noisy radiance to RR's NN - matching the AMD sample, which sends complete
    // irradiance from its tracer. The old residual-only path (raw - floor) destroyed all
    // low-frequency detail before the NN ever saw it: the floor re-broadcast it blurred via
    // SkipSignal, producing the vaseline look by construction.
    //
    // Firefly compression stays, but on FULL radiance with a generous threshold: only true
    // energy spikes (>4x local median) are damped, and they keep 40% of their excess so
    // highlights retain bite.
    float3 signalColor = rawColor;"""

if old not in c:
    # The current file still has the OLD residual block (raw - floor). Replace it:
    old2 = """// Signal denoising pre-pass: compress fireflies/outliers in the residual BEFORE demodulation.
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
    const float3 denoiserColor = max(signalColor, 0.0f);"""
    new2 = """// HONEST SIGNAL: feed the NN the complete demodulated radiance, exactly like the AMD sample's
    // tracer feeds its denoiser. The old residual-only path (raw - blurredFloor) destroyed all
    // low-frequency detail pre-NN and re-broadcast it blurred via SkipSignal - the structural
    // cause of the vaseline look. Fireflies are clamped generously (true energy spikes only).
    float3 signalColor = rawColor;
    {
        const float signalLuma = GetLuminance(signalColor);
        const float floorRef = max(floorLuma, 1e-3f);
        const float spikeLimit = floorRef * 4.0f + 0.05f;

        if (signalLuma > spikeLimit)
        {
            const float excess = signalLuma - spikeLimit;
            const float compressed = spikeLimit + excess * 0.4f;
            signalColor *= compressed / signalLuma;
        }
    }
    const float3 denoiserColor = max(signalColor, 0.0f);"""
    assert old2 in c, "residual block not found"
    c = c.replace(old2, new2, 1)
else:
    print("already honest")

io.open(p, "w", encoding="utf-8").write(c)
print("signal made honest")
