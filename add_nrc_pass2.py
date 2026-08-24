import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

# Trailing spaces after "blSeedByteCode," etc - use regex-free line-based replace
old = "        bool isMode2\n    )"
if old in c:
    c = c.replace(old, "        bool isMode2,\n        std::span<const byte> nrcQueryByteCode = {}\n    )", 1)
    print("signature patched")
else:
    # try with \r
    old2 = "        bool isMode2\r\n    )"
    if old2 in c:
        c = c.replace(old2, "        bool isMode2,\r\n        std::span<const byte> nrcQueryByteCode = {}\r\n    )", 1)
        print("signature patched (crlf)")
    else:
        raise SystemExit("signature not found")

old_init = '        m_compShader.Initialize(m_pDev, compByteCode, sizeof(Composition::Constants), \r\n            Composition::Input::kCount, Composition::kOutputCount, L"FSRD_Comp_Constants", Composition::kBackBufferCount);'
new_init = old_init + "\r\n\r\n        if (!nrcQueryByteCode.empty())\r\n            m_nrcQueryShader.Initialize(m_pDev, nrcQueryByteCode, sizeof(NrcQueryConstants),\r\n                3u, 1u, L\"FSRD_NrcQuery_Constants\", FloorSeed::kBackBufferCount);"
if old_init in c:
    c = c.replace(old_init, new_init, 1)
    print("init patched")
else:
    old_init2 = '        m_compShader.Initialize(m_pDev, compByteCode, sizeof(Composition::Constants), \n            Composition::Input::kCount, Composition::kOutputCount, L"FSRD_Comp_Constants", Composition::kBackBufferCount);'
    new_init2 = old_init2 + "\n\n        if (!nrcQueryByteCode.empty())\n            m_nrcQueryShader.Initialize(m_pDev, nrcQueryByteCode, sizeof(NrcQueryConstants),\n                3u, 1u, L\"FSRD_NrcQuery_Constants\", FloorSeed::kBackBufferCount);"
    if old_init2 in c:
        c = c.replace(old_init2, new_init2, 1)
        print("init patched (lf)")
    else:
        raise SystemExit("init not found")

io.open(p, "w", encoding="utf-8").write(c)
