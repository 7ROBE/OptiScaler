import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/precompile/FSRDInputConv.hlsl"
c = io.open(p, encoding="utf-8").read()

# THE REAL FIX: feed the NN the FULL noisy radiance, not floor-subtracted residual.
# The residual-only architecture was built for FFX 2.x; RR's NN is trained on complete
# noisy radiance (the AMD sample feeds full irradiance). Detail absorbed by the floor
# is currently re-broadcast blurred via SkipSignal - that's the vaseline.
#
# Keep: spike compression (fireflies) applied to the FULL radiance now.
# Floor stays ONLY for the composition stabilization path (SkipSignal), its original role.
old = """// Signal denoising pre-pass: compress fireflies/outliers in the residual BEFORE demodulation.
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

new = """// Feed the FULL noisy radiance to RR's NN - matching the AMD sample, which sends complete
    // irradiance from its tracer. The old residual-only path (raw - floor) destroyed all
    // low-frequency detail before the NN ever saw it: the floor re-broadcast it blurred via
    // SkipSignal, producing the vaseline look by construction.
    //
    // Firefly compression stays, but on FULL radiance with a generous threshold: only true
    // energy spikes (>4x local median) are damped, and they keep 40% of their excess so
    // highlights retain bite.
    float3 signalColor = rawColor;
    {
        const float signalLuma = GetLuminance(signalColor);
        const float floorRef = max(floorLuma, 1e-3f);
        const float spikeLimit = floorRef * 4.0f + 0.05f;

        if (signalLuma > spikeLimit)
        {
            const float excess = signalLuma - spikeLimit;
            const float compressed = spikeLimit + excess * 0.4f;
            signalColor *= compressed / signalLuma; // preserve chroma ratio, scale magnitude
        }
    }
    const float3 denoiserColor = max(signalColor, 0.0f);"""

assert old in c
c = c.replace(old, new, 1)

# The knee's localRef uses floorLuma*rcp(diffDiv) - with full radiance signals this still works
# as a rough local brightness reference. Keep.

io.open(p, "w", encoding="utf-8").write(c)
print("full radiance signal applied")
