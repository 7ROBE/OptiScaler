import io

p2 = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c2 = io.open(p2, encoding="utf-8").read()

anchor = "bool FSRDPreprocessor_Dx12::DispatchConversion(ID3D12GraphicsCommandList* cmdList, const ConversionDesc& desc)"
assert anchor in c2
accessors = """ID3D12Resource* FSRDPreprocessor_Dx12::GetLinearDepth() const { return m_LinearDepth.Get(); }
ID3D12Resource* FSRDPreprocessor_Dx12::GetOutputNormals() const
{
    return m_outputBuffer1.Get();
}
ID3D12Resource* FSRDPreprocessor_Dx12::GetOutputDiffAlbedo() const
{
    return m_outputBuffer2.Get();
}

"""
c2 = c2.replace(anchor, accessors + anchor, 1)
io.open(p2, "w", encoding="utf-8").write(c2)
print("cpp accessors added")
