import io, re

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

# 1. Fix m_nrcSrcSize assignment type (desc.RenderSize is XMFLOAT2? check error at 455 - assignment of
#    XMFLOAT4 to something). Look: line 455 error = binary '=' with const XMFLOAT4 right operand.
#    That's probably `m_smoothFloor = ...` no... it's my capture block using desc.RenderSize which may be
#    XMFLOAT4? Simplest: cast via float values.
old = """        // Capture for the NRC query pass (same frame matrices)
        m_nrcInvViewMatrix = desc.InvViewMatrix;
        m_nrcInvProjMatrix = desc.InvProjMatrix;
        m_nrcSrcSize = desc.RenderSize;"""
new = """        // Capture for the NRC query pass (same frame matrices)
        m_nrcInvViewMatrix = desc.InvViewMatrix;
        m_nrcInvProjMatrix = desc.InvProjMatrix;
        m_nrcSrcSize = XMFLOAT2(desc.RenderSize.x, desc.RenderSize.y);"""
if old in c:
    c = c.replace(old, new, 1)
    print("capture fixed")
else:
    print("capture pattern differs - inspect")

# 2. NrcQuery::Constants -> NrcQueryConstants
c = c.replace("NrcQuery::Constants", "NrcQueryConstants")

io.open(p, "w", encoding="utf-8").write(c)
print("done")
