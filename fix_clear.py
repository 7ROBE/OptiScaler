import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

# Clear the signal history on creation (alpha=0 = invalid everywhere => clean start).
# CreateTex leaves content undefined. Add a clear after creation - need a command list.
# Simplest robust approach: track first-use and clear via the frame's command list.
old = """        m_SignalHistory = CreateTex(FSRDFormats::SkipSignal, L"FSR_Conv_SignalHistory");"""
new = """        m_SignalHistory = CreateTex(FSRDFormats::SkipSignal, L"FSR_Conv_SignalHistory");
        m_signalHistoryNeedsClear = true;"""
assert old in c
c = c.replace(old, new, 1)

# Member flag
old2 = """    ComPtr<ID3D12Resource> m_SignalHistory;"""
new2 = """    ComPtr<ID3D12Resource> m_SignalHistory;
    bool m_signalHistoryNeedsClear = false;"""
assert old2 in c
c = c.replace(old2, new2, 1)

# Clear at dispatch time before binding (UAV clear with 0 alpha = invalid everywhere)
old3 = """        in.Resources.InPrevSignal = m_SignalHistory.Get();"""
new3 = """        if (m_signalHistoryNeedsClear)
        {
            const UINT clearValue[4] = { 0, 0, 0, 0 }; // alpha 0 = invalid => no history blending
            cmdList->ClearUnorderedAccessViewUint(
                m_frameHeaps.empty() ? nullptr : nullptr, nullptr,
                m_SignalHistory.Get(), clearValue, 0, nullptr);
            m_signalHistoryNeedsClear = false;
        }
        in.Resources.InPrevSignal = m_SignalHistory.Get();"""
assert old3 in c
c = c.replace(old3, new3, 1)

io.open(p, "w", encoding="utf-8").write(c)
print("clear added (needs descriptor refinement)")
