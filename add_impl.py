import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

# Add the impl method (inside Impl, before DispatchConversion)
anchor = "    void DispatchConversion(ID3D12GraphicsCommandList* cmdList, const ConversionDesc& desc) "
if anchor not in c:
    anchor = "    void DispatchConversion(ID3D12GraphicsCommandList* cmdList, const ConversionDesc& desc) \r\n"
assert anchor in c, "anchor missing"

method = """    bool DispatchNrcQuery(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth,
                          ID3D12Resource* normals, ID3D12Resource* diffAlbedo,
                          ID3D12Resource* queryBuffer, ID3D12Device* dev, UINT queryCount)
    {
        if (!cmdList || !queryBuffer || !dev || !m_nrcSrcSize.x)
            return false;

        NrcQueryConstants constants = {};
        constants.InvViewMatrix = m_nrcInvViewMatrix;
        constants.InvProjMatrix = m_nrcInvProjMatrix;
        constants.Flags = 0;
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
print("impl method added")
