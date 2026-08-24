import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.h"
c = io.open(p, encoding="utf-8").read()

# Remove the OLD duplicate declaration (without countersBuffer)
old = """    /// Dispatches the NRC query-generation pass. Fills the caller-owned query buffer with
    /// packed NRC queries from the converted signal textures.
    bool DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
                          ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
                          ID3D12Resource* queryBuffer, UINT queryCount);"""
assert old in c
c = c.replace(old, "", 1)
io.open(p, "w", encoding="utf-8").write(c)
print("duplicate decl removed")
