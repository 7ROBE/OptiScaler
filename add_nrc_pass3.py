import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

# 1. Add NrcQueryConstants struct definition before the Impl struct
anchor_struct = "// Private implementation\r\nstruct FSRDPreprocessor_Dx12::Impl"
if anchor_struct not in c:
    anchor_struct = "// Private implementation\nstruct FSRDPreprocessor_Dx12::Impl"
assert anchor_struct in c, "impl anchor missing"

nrc_const = """// NRC query-generation constants (FSRDNrcQuery.hlsl)
struct NrcQueryConstants
{
    XMFLOAT4X4 InvViewMatrix;
    XMFLOAT4X4 InvProjMatrix;
    float NearPlane;
    float FarPlane;
    UINT Flags;
    UINT QueryCount;
    UINT SrcWidth;
    UINT SrcHeight;
};

"""

c = c.replace(anchor_struct, nrc_const + anchor_struct, 1)

# 2. Add state capture members to Impl (after m_nrcQueryShader member)
m = "    ComputeState m_nrcQueryShader;"
assert m in c
c = c.replace(m, m + """
    // Last conversion matrices for the NRC query pass (matches ConversionDesc at pack time)
    XMFLOAT4X4 m_nrcInvViewMatrix;
    XMFLOAT4X4 m_nrcInvProjMatrix;
    XMFLOAT2 m_nrcSrcSize;""", 1)

# 3. Capture matrices inside DispatchPackingShader (after packConstants built)
old_pack = """        in.Resources.InBlurColor = m_smoothFloor;
        in.Resources.InPrevFloorColor = m_FloorHistory.Get();"""
new_pack = """        in.Resources.InBlurColor = m_smoothFloor;
        in.Resources.InPrevFloorColor = m_FloorHistory.Get();

        // Capture for the NRC query pass (same frame matrices)
        m_nrcInvViewMatrix = desc.InvViewMatrix;
        m_nrcInvProjMatrix = desc.InvProjMatrix;
        m_nrcSrcSize = desc.RenderSize;"""
assert old_pack in c
c = c.replace(old_pack, new_pack, 1)

# 4. Add DispatchNrcQuery method into Impl before DispatchConversion
anchor = "    void DispatchConversion(ID3D12GraphicsCommandList* cmdList, const ConversionDesc& desc) "
if anchor not in c:
    anchor = anchor.rstrip() + "\r\n"

method = """    bool DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
                          ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
                          ID3D12Resource* queryBuffer, ID3D12Device* dev, UINT queryCount)
    {
        if (!cmdList || !queryBuffer || !dev || !m_nrcSrcSize.x)
            return false;

        NrcQuery::Constants constants = {};
        constants.InvViewMatrix = m_nrcInvViewMatrix;
        constants.InvProjMatrix = m_nrcInvProjMatrix;
        constants.QueryCount = queryCount;
        constants.SrcWidth = (UINT) m_nrcSrcSize.x;
        constants.SrcHeight = (UINT) m_nrcSrcSize.y;

        ID3D12Resource* inputs[] = { normals, diffAlbedo, depth };
        ID3D12Resource* outputs[] = { queryBuffer };

        const std::span<const byte> cbData((const byte*) &constants, sizeof(constants));
        m_nrcQueryShader.Dispatch(cmdList, cbData, inputs, outputs,
                                  XMFLOAT2(m_nrcSrcSize.x / 2.0f, m_nrcSrcSize.y / 2.0f), false);
        return true;
    }

"""

c = c.replace(anchor, method + anchor, 1)

io.open(p, "w", encoding="utf-8").write(c)
print("all preprocessor changes applied")
