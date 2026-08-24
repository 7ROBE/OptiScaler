import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

old = """        constants.QueryCount = queryCount;
        constants.SrcWidth = (UINT) m_nrcSrcSize.x;
        constants.SrcHeight = (UINT) m_nrcSrcSize.y;

        ID3D12Resource* inputs[] = { normals, diffAlbedo, depth };
        ID3D12Resource* outputs[] = { queryBuffer };"""

new = """        constants.QueryCount = queryCount;
        constants.SrcWidth = (UINT) m_nrcSrcSize.x;
        constants.SrcHeight = (UINT) m_nrcSrcSize.y;
        constants.SceneScale = 2000.0f; // ~2km scene extent heuristic

        ID3D12Resource* inputs[] = { normals, diffAlbedo, depth };
        ID3D12Resource* outputs[] = { queryBuffer, countersBuffer };"""

assert old in c
c = c.replace(old, new, 1)

old_sig = """    bool DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
                          ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
                          ID3D12Resource* queryBuffer, ID3D12Device* dev, UINT queryCount)"""
new_sig = """    bool DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
                          ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
                          ID3D12Resource* queryBuffer, ID3D12Resource* countersBuffer,
                          ID3D12Device* dev, UINT queryCount)"""
assert old_sig in c
c = c.replace(old_sig, new_sig, 1)

io.open(p, "w", encoding="utf-8").write(c)
print("cpp fixed")

ph = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.h"
ch = io.open(ph, encoding="utf-8").read()
if "countersBuffer" not in ch:
    old_h = """    bool DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
                          ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
                          ID3D12Resource* queryBuffer, ID3D12Device* dev, UINT queryCount);"""
    new_h = """    bool DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
                          ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
                          ID3D12Resource* queryBuffer, ID3D12Resource* countersBuffer,
                          ID3D12Device* dev, UINT queryCount);"""
    assert old_h in ch
    ch = ch.replace(old_h, new_h, 1)
    io.open(ph, "w", encoding="utf-8").write(ch)
    print("header fixed")
