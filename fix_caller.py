import io

# Update the FSRDFeature caller to pass nrcBufE (counters) and set SceneScale-related state
p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/upscalers/fsr31/FSRDFeature_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

old = """        FSRDConvShader->DispatchNrcQuery(InCommandList,
            FSRDConvShader->GetLinearDepth(),
            FSRDConvShader->GetOutputNormals(),
            FSRDConvShader->GetOutputDiffAlbedo(),
            nrcBufA.Get(), Device,
            RenderWidth() * RenderHeight() / 4);"""
new = """        FSRDConvShader->DispatchNrcQuery(InCommandList,
            FSRDConvShader->GetLinearDepth(),
            FSRDConvShader->GetOutputNormals(),
            FSRDConvShader->GetOutputDiffAlbedo(),
            nrcBufA.Get(), nrcBufE.Get(), Device,
            RenderWidth() * RenderHeight() / 4);"""
assert old in c
c = c.replace(old, new, 1)

io.open(p, "w", encoding="utf-8").write(c)
print("caller updated")
