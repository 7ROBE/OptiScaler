import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

sig = "ID3D12Resource* FSRDPreprocessor_Dx12::GetCompositionOutput() const"
assert sig in c, "sig missing"

# Find the end of this function: first "}"+newline after the signature
start = c.find(sig)
end_brace = c.find("}", start)
insert_at = end_brace + 1

wrapper = """

bool FSRDPreprocessor_Dx12::DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
    ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
    ID3D12Resource* queryBuffer, ID3D12Device* dev, UINT queryCount)
{
    return m_impl->DispatchNrcQuery(cmdList, depth, normals, diffAlbedo, queryBuffer, dev, queryCount);
}"""

c = c[:insert_at] + wrapper + c[insert_at:]
io.open(p, "w", encoding="utf-8").write(c)
print("wrapper added after GetCompositionOutput")
