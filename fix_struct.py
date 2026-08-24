import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

# 1. Add SceneScale to struct (the earlier fix_docs didn't persist due to assert ordering)
old_struct = """    UINT QueryCount;
    UINT SrcWidth;
    UINT SrcHeight;
};"""
new_struct = """    UINT QueryCount;
    UINT SrcWidth;
    UINT SrcHeight;
    float SceneScale;   // world units for normalized position 1.0
};"""
assert old_struct in c
c = c.replace(old_struct, new_struct, 1)

# 2. Fix the out-of-class wrapper (771) - add countersBuffer param
old_wrap = """bool FSRDPreprocessor_Dx12::DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
    ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
    ID3D12Resource* queryBuffer, ID3D12Device* dev, UINT queryCount)
{
    return m_impl->DispatchNrcQuery(cmdList, depth, normals, diffAlbedo, queryBuffer, dev, queryCount);"""
if old_wrap not in c:
    # try alternate formatting
    import re
    m = re.search(r"bool FSRDPreprocessor_Dx12::DispatchNrcQuery\(.*?\{.*?\}", c, re.DOTALL)
    print("current wrapper:", c[m.start():m.end()][:400] if m else "not found")
else:
    c = c.replace(old_wrap, """bool FSRDPreprocessor_Dx12::DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
    ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
    ID3D12Resource* queryBuffer, ID3D12Resource* countersBuffer,
    ID3D12Device* dev, UINT queryCount)
{
    return m_impl->DispatchNrcQuery(cmdList, depth, normals, diffAlbedo, queryBuffer, countersBuffer, dev, queryCount);""", 1)

io.open(p, "w", encoding="utf-8").write(c)
print("fixed")
