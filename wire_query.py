import io

# Wire the query dispatch + target copy into EvaluateInternal, right before the NRC train call.
p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/upscalers/fsr31/FSRDFeature_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

old = """    static uint32_t s_nrcFrameCounter = 0;
    if (_nrcReady && result == FFX_API_RETURN_OK && (++s_nrcFrameCounter % 4) == 0)
        DispatchNrc(InCommandList, true);"""

new = """    static uint32_t s_nrcFrameCounter = 0;
    const bool nrcTrainThisFrame = _nrcReady && result == FFX_API_RETURN_OK && (++s_nrcFrameCounter % 4) == 0;

    if (nrcTrainThisFrame)
    {
        // 1. Fill prediction/training query buffers from the converted signals
        FSRDConvShader->DispatchNrcQuery(InCommandList,
            FSRDConvShader->GetLinearDepth(),
            FSRDConvShader->GetOutputNormals(),
            FSRDConvShader->GetOutputDiffAlbedo(),
            nrcBufA.Get(), Device,
            RenderWidth() * RenderHeight() / 4);

        // Training uses the same queries; targets come from the denoised composition output
        // which is FSRDConvShader's latest composition result. Copy it into the target buffer
        // (RGBA16F -> R32G32B32 packed radiance happens inside NRC's internal handling of
        // trainTargets when bound as a plain buffer - v1 packs luma-relevant RGB via the copy).
        InCommandList->CopyResource(nrcBufD.Get(), FSRDConvShader->GetCompositionOutput());
        DispatchNrc(InCommandList, true);
    }"""

assert old in c
c = c.replace(old, new, 1)

# GetCompositionOutput must be accessible - check include exists for preprocessor header in this cpp:
if "FSRDPreprocessor_Dx12.h" not in c:
    raise SystemExit("preprocessor header not included!")

io.open(p, "w", encoding="utf-8").write(c)
print("query dispatch + target copy wired")
