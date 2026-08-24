import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/upscalers/fsr31/FSRDFeature_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

# Add step-by-step logging to find the exact faulting call
old = """        // 1. Fill prediction/training query buffers from the converted signals
        FSRDConvShader->DispatchNrcQuery(InCommandList,
            FSRDConvShader->GetLinearDepth(),
            FSRDConvShader->GetOutputNormals(),
            FSRDConvShader->GetOutputDiffAlbedo(),
            nrcBufA.Get(), Device,
            RenderWidth() * RenderHeight() / 4);

        // Training targets: the denoised composition output (texture) bound directly as the
        // NRC trainTargets resource - no intermediate copy needed.
        nrcTrainTgtRes = ffxApiGetResourceDX12(FSRDConvShader->GetCompositionOutput(),
                                               FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        DispatchNrc(InCommandList, false); // training disabled: trainTargets must be a BUFFER, texture binding TDRs the device"""

new = """        LOG_WARN("NRC step 1: barriers done");
        
        // 1. Fill prediction/training query buffers from the converted signals
        LOG_WARN("NRC step 2: dispatching query shader");
        FSRDConvShader->DispatchNrcQuery(InCommandList,
            FSRDConvShader->GetLinearDepth(),
            FSRDConvShader->GetOutputNormals(),
            FSRDConvShader->GetOutputDiffAlbedo(),
            nrcBufA.Get(), Device,
            RenderWidth() * RenderHeight() / 4);
        LOG_WARN("NRC step 3: query shader dispatched");

        // Training targets: the denoised composition output (texture) bound directly as the
        // NRC trainTargets resource - no intermediate copy needed.
        nrcTrainTgtRes = ffxApiGetResourceDX12(FSRDConvShader->GetCompositionOutput(),
                                               FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        LOG_WARN("NRC step 4: calling NRC dispatch (inference)");
        DispatchNrc(InCommandList, false);
        LOG_WARN("NRC step 5: NRC dispatch returned");"""

assert old in c
c = c.replace(old, new, 1)
io.open(p, "w", encoding="utf-8").write(c)
print("step logging added")
