# Insert NRC implementation after DispatchDenoiser in FSRDFeature_Dx12.cpp
import io

path = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/upscalers/fsr31/FSRDFeature_Dx12.cpp"
with io.open(path, encoding="utf-8") as f:
    content = f.read()

nrc = """
// ---------------------------------------------------------------------------
// FidelityFX Neural Radiance Cache (NRC) - optional far-field stabilization.
// Queries: position(3) octaNormal(2) octaViewDir(2) diffuseAlbedo(3) roughness(1) = 11 floats
// Output: radiance(3). Training target = FFX-RR denoised radiance (near-field detail stays with RR).
// ---------------------------------------------------------------------------
struct NrcQuery
{
    float position[3];
    float normal[2];
    float viewDir[2];
    float diffuseAlbedo[3];
    float roughness;
};

static ComPtr<ID3D12Resource> CreateNrcBuffer(ID3D12Device* dev, UINT64 byteSize, const wchar_t* name)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = byteSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Resource> res;
    if (FAILED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&res))))
        return nullptr;

    if (name)
        res->SetName(name);
    return res;
}

bool FSRDFeatureDx12::InitNrc(ID3D12GraphicsCommandList* cmdList)
{
    const UINT queryCount = RenderWidth() * RenderHeight() / 4; // quarter-res queries

    ffxCreateContextDescRadianceCache nrcDesc = {};
    nrcDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_RADIANCECACHE;
    nrcDesc.version = FFX_RADIANCECACHE_VERSION;
    nrcDesc.maxInferenceSampleCount = queryCount;
    nrcDesc.maxTrainingSampleCount = queryCount;

    auto ret = FfxApiProxy::D3D12_CreateContext(&_pNrcCtx, &nrcDesc.header, NULL);
    if (ret != FFX_API_RETURN_OK)
    {
        LOG_ERROR("NRC context creation failed: {0}", FfxApiProxy::ReturnCodeToString(ret));
        return false;
    }

    _nrcPredIn = CreateNrcBuffer(Device, UINT64(queryCount) * sizeof(NrcQuery), L"FSRD_NRC_PredIn");
    _nrcPredOut = CreateNrcBuffer(Device, UINT64(queryCount) * sizeof(float) * 3, L"FSRD_NRC_PredOut");
    _nrcTrainIn = CreateNrcBuffer(Device, UINT64(queryCount) * sizeof(NrcQuery), L"FSRD_NRC_TrainIn");
    _nrcTrainTgt = CreateNrcBuffer(Device, UINT64(queryCount) * sizeof(float) * 3, L"FSRD_NRC_TrainTgt");
    _nrcCounters = CreateNrcBuffer(Device, sizeof(uint32_t) * 2, L"FSRD_NRC_Counters");

    if (!_nrcPredIn || !_nrcPredOut || !_nrcTrainIn || !_nrcTrainTgt || !_nrcCounters)
    {
        LOG_ERROR("NRC buffer creation failed.");
        DestroyNrc();
        return false;
    }

    _nrcPredInRes = ffxApiGetResourceDX12(_nrcPredIn.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    _nrcPredOutRes = ffxApiGetResourceDX12(_nrcPredOut.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    _nrcTrainInRes = ffxApiGetResourceDX12(_nrcTrainIn.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    _nrcTrainTgtRes = ffxApiGetResourceDX12(_nrcTrainTgt.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    _nrcCountersRes = ffxApiGetResourceDX12(_nrcCounters.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

    _nrcReady = true;
    LOG_INFO("NRC initialized - query count: {0}", queryCount);
    return true;
}

void FSRDFeatureDx12::DestroyNrc()
{
    if (_pNrcCtx)
        FfxApiProxy::D3D12_DestroyContext(&_pNrcCtx);
    _pNrcCtx = nullptr;
    _nrcReady = false;
}

bool FSRDFeatureDx12::DispatchNrc(ID3D12GraphicsCommandList* InCommandList, bool train)
{
    if (!_nrcReady || !_pNrcCtx)
        return true; // not fatal - just no far-field stabilization

    ffxDispatchDescRadianceCache nrcDispatch = {};
    nrcDispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_RADIANCECACHE;
    nrcDispatch.commandList = InCommandList;
    nrcDispatch.predictionInputs = _nrcPredInRes;
    nrcDispatch.predictionOutputs = _nrcPredOutRes;
    nrcDispatch.trainInputs = _nrcTrainInRes;
    nrcDispatch.trainTargets = _nrcTrainTgtRes;
    nrcDispatch.sampleCounters = _nrcCountersRes;
    nrcDispatch.flags = FFX_RADIANCE_CACHE_DISPATCH_INFERENCE |
                        (train ? FFX_RADIANCE_CACHE_DISPATCH_TRAINING : 0);

    const ffxReturnCode_t result = FfxApiProxy::D3D12_Dispatch(&_pNrcCtx, &nrcDispatch.header);
    if (result != FFX_API_RETURN_OK)
    {
        LOG_WARN("NRC dispatch failed: {0} - disabling NRC for this session", (UINT) result);
        _nrcReady = false;
        return false;
    }
    return true;
}
"""

marker = "void FSRDFeatureDx12::SetDefaultConfiguration()"
idx = content.find(marker)
assert idx > 0, "marker not found"

content = content[:idx] + nrc + "\n" + content[idx:]
with io.open(path, "w", encoding="utf-8") as f:
    f.write(content)
print("inserted NRC block before SetDefaultConfiguration")
