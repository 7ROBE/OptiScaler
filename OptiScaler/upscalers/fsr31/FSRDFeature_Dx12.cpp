#include "pch.h"
#include <nvsdk_ngx_defs_dlssd.h>
#include <DirectXMath.h>
#include "NVNGX_Parameter.h"
#include "hooks/Streamline_Hooks.h"
#include "FSRDFeature_Dx12.h"
#include "shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.h"
#include "MathUtils.h"

// FSR Ray Regeneration 1.2.0 / FidelityFX SDK 2.3.0
// Major breaking API changes applied:
//   - Removed: deltaTime, _reserved, mode, fpMessage from descriptors
//   - Removed: FfxApiDenoiserMode, fused dispatch structs (Input1/2/4Signals)
//   - Removed: ffxQueryDescDenoiserGetVersion (version is now DLL/provider embedded)
//   - Added: view + projection matrix fields replace camera component fields
//   - Added: signalFlags + checkerboardSignalFlags on context creation
//   - Added: per-signal dispatch descriptors (IndirectSpecular, IndirectDiffuse,
//            DirectDiffuse, DirectSpecular, AmbientOcclusion, SpecularOcclusion, DominantLight)
//   - Added: linearDepthBounds (passthrough API)
//   - Added: checkerboardOrigin on FfxApiDenoiserSignal
//   - Changed: linearDepth is now SIGNED; motionVectors.z is now SIGNED delta
//   - Changed: FFX_DENOISER_ENABLE_DOMINANT_LIGHT -> FFX_DENOISER_SIGNAL_DOMINANT_LIGHT_VISIBILITY
//   - Changed: fpMessage replaced by ffxConfigureDescGlobalDebug
//   - Changed: ffxDispatchDescDenoiserInputDominantLight -> ffxDispatchDescDenoiserDominantLight

using namespace DirectX;
using namespace OptiMath;

using FSRDConvDesc = FSRDPreprocessor_Dx12::ConversionDesc;
using FSRDCompDesc = FSRDPreprocessor_Dx12::CompositionDesc;

/**
 * @brief Retrieves a matrix from the given parameter table. Matrices used by DLSS are in column-major
 * order, but DirectXMath operations assume row-major.
 */
static bool TryGetNGXMatrixTranspose(const NVSDK_NGX_Parameter& ngxParams, const char* key,
                                     DirectX::XMMATRIX& outValue)
{
    float* pMat = nullptr;

    if (ngxParams.Get(key, (void**) &pMat) == NVSDK_NGX_Result_Success && pMat != nullptr)
    {
        memcpy_s(&outValue, sizeof(DirectX::XMMATRIX), pMat, sizeof(float) * 16);
        return true;
    }
    else
        return false;
}

/**
 * @brief Retrieves a matrix from the given parameter table and transposes it for CPU-side
 * operations with DirectXMath.
 */
static bool TryGetNGXMatrix(const NVSDK_NGX_Parameter& ngxParams, const char* key,
                             DirectX::XMMATRIX& outValue)
{
    if (TryGetNGXMatrixTranspose(ngxParams, key, outValue))
    {
        outValue = XMMatrixTranspose(outValue);
        return true;
    }
    else
        return false;
}

template <typename T>
static bool TryGetLoggedResource(const NVSDK_NGX_Parameter& ngxParams, const char* key, T*& outValue)
{
    const bool success = TryGetNGXVoidPointer(ngxParams, key, outValue);

    if (success)
        LOG_DEBUG("{} exists..", key);
    else
        LOG_ERROR("{} is missing!!", key);

    return success;
}

/**
 * @brief Calculates vertical FOV: FOVv = 2 * arctan( 1 / M22 )
 */
static float GetVertFovFromProjectionMatrixRad(const XMMATRIX& proj)
{
    return float(2.0 * (std::atan(1.0 / (double) proj.r[1].m128_f32[1])));
}

/**
 * @brief Calculates horizontal FOV: FOVh = 2 * arctan( 1 / M11 )
 */
static float GetHorzFovFromProjectionMatrixRad(const XMMATRIX& proj)
{
    return float(2.0 * (std::atan(1.0 / (double) proj.r[0].m128_f32[0])));
}

/**
 * @brief Calculates aspect ratio: AR = M22 / M11
 */
static float GetAspectRatioFromProjectionMatrix(const XMMATRIX& proj)
{
    return proj.r[1].m128_f32[1] / proj.r[0].m128_f32[0];
}

static XMFLOAT3 GetFloat3(const XMVECTOR& vec4)
{
    XMFLOAT3 vec3 = {};
    XMStoreFloat3(&vec3, vec4);
    return vec3;
}

static XMVECTOR GetColumn(const XMMATRIX& mat, int col)
{
    return { mat.r[0].m128_f32[col], mat.r[1].m128_f32[col], mat.r[2].m128_f32[col], 0 };
}

static void SetColumn(const XMVECTOR& vec, int col, XMMATRIX& mat)
{
    mat.r[0].m128_f32[col] = vec.m128_f32[0];
    mat.r[1].m128_f32[col] = vec.m128_f32[1];
    mat.r[2].m128_f32[col] = vec.m128_f32[2];
    mat.r[3].m128_f32[col] = vec.m128_f32[3];
}

static XMFLOAT3 GetFloat3Column(const XMMATRIX& mat, int col)
{
    return { mat.r[0].m128_f32[col], mat.r[1].m128_f32[col], mat.r[2].m128_f32[col] };
}

static FfxApiFloatCoords3D GetFloat3ColumnFFX(const XMMATRIX& mat, int col)
{
    return { mat.r[0].m128_f32[col], mat.r[1].m128_f32[col], mat.r[2].m128_f32[col] };
}

static FfxApiFloatCoords3D GetFloat3FFX(const XMVECTOR& vec4)
{
    FfxApiFloatCoords3D vec3 = {};
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&vec3), vec4);
    return vec3;
}

static const FfxApiFloatCoords3D& GetFloat3FFX(const XMFLOAT3& vec3)
{
    return *reinterpret_cast<const FfxApiFloatCoords3D*>(&vec3);
}

static ID3D12Resource* GetD3D12ResFromFFX(const FfxApiResource& resource)
{
    return static_cast<ID3D12Resource*>(resource.resource);
}

/**
 * @brief Stores a 4x4 XMMATRIX into an FfxApiFloatMatrix44 in row-major order.
 * SDK 2.3.0 view/projection fields are FfxApiFloatMatrix44 (row-major float[4][4]).
 */
static void StoreFfxMatrix(const XMMATRIX& src, FfxApiFloatMatrix44& dst)
{
    // XMMATRIX rows are XMVECTOR (SSE aligned). FfxApiFloatMatrix44 is float[4][4] row-major.
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++)
            dst.m[row][col] = src.r[row].m128_f32[col];
}

struct ViewPlanes
{
    float nearPlane;
    float farPlane;
    bool isInfinite;
    bool isRightHanded;
};

static ViewPlanes GetViewPlanes(const DirectX::XMMATRIX& projection, bool isInverted)
{
    ViewPlanes planes;
    float A = projection.r[2].m128_f32[2];
    float B = projection.r[2].m128_f32[3];
    float W = projection.r[3].m128_f32[2];

    float infiniteCheckVal = isInverted ? A : (A - W);
    planes.isInfinite = std::abs(infiniteCheckVal) < 1e-6f;
    planes.isRightHanded = B < 0.0f;

    if (isInverted)
    {
        planes.nearPlane = std::abs(B / (W - A));
        planes.farPlane = std::abs(-B / A);
    }
    else
    {
        planes.nearPlane = std::abs(-B / A);
        planes.farPlane = std::abs(B / (W - A));
    }

    return planes;
}

using FSRDConvFlags = FSRDPreprocessor_Dx12::ConvFlags;
using FSRDCompFlags = FSRDPreprocessor_Dx12::CompFlags;

enum class DebugModes : uint64_t
{
    None = 0,
    DenoiserBypass = 1,
    UpscalerBypass = 2,
    RawColor = 3,
    DlssBias = 4,
    DlssColorBeforeParticles = 5,
    DlssColorBeforeTransparency = 6,
    DlssTransparencyLayer = 7,
    FfxDebug = 8,

    ConversionDebug = FSRDConvFlags::Debug,
    ConversionDebugMask = FSRDConvFlags::DebugModeMask,

    OutRadiance = FSRDConvFlags::DebugOutRadiance,

    InSpecHitDist = FSRDConvFlags::DebugInSpecHitDist,
    InMotion = FSRDConvFlags::DebugInMotion,
    InNormals = FSRDConvFlags::DebugInNormals,
    InRoughness = FSRDConvFlags::DebugInRoughness,
    InDiffAlbedo = FSRDConvFlags::DebugInDiffAlbedo,
    InSpecAlbedo = FSRDConvFlags::DebugInSpecAlbedo,

    OutFusedAlbedo = FSRDConvFlags::DebugOutFusedAlbedo,
    OutLinearDepth = FSRDConvFlags::DebugOutLinearDepth,
    OutMotion = FSRDConvFlags::DebugOutMotion,
    OutNormals = FSRDConvFlags::DebugOutNormals,
    OutSpecAlbedo = FSRDConvFlags::DebugOutSpecAlbedo,
    OutDiffAlbedo = FSRDConvFlags::DebugOutDiffAlbedo,

    OutDepthDelta = FSRDConvFlags::DebugOutDepthDelta,
    NormDepth = FSRDConvFlags::DebugNormDepth,
    AlbedoError = FSRDConvFlags::DebugAlbedoError,

    FloorVariance = FSRDConvFlags::DebugFloorVariance,
    FloorColor = FSRDConvFlags::DebugFloorColor,

    CompositionDebugOffset = 16u,
    CompositionDebug = (uint64_t) FSRDCompFlags::Debug << CompositionDebugOffset,
    CompositionDebugMask = (uint64_t) FSRDCompFlags::DebugModeMask,

    Correlation = (uint64_t) FSRDCompFlags::DebugCorrelation << CompositionDebugOffset,
    SkipSignal = (uint64_t) FSRDCompFlags::DebugSkipSignal << CompositionDebugOffset,
    DenoiserOutput = (uint64_t) FSRDCompFlags::DebugDenoiserOutput << CompositionDebugOffset,
    Signal1 = (uint64_t) FSRDCompFlags::DebugSignal1 << CompositionDebugOffset,
    Signal2 = (uint64_t) FSRDCompFlags::DebugSignal2 << CompositionDebugOffset,
};

static FSRDConvFlags GetConvDebugFlags(DebugModes mode)
{
    uint32_t flags = uint32_t(mode);
    flags &= uint32_t(DebugModes::ConversionDebugMask);
    return FSRDConvFlags(flags);
}

static FSRDCompFlags GetCompDebugFlags(DebugModes mode)
{
    uint64_t flags = uint64_t(mode);
    flags >>= uint64_t(DebugModes::CompositionDebugOffset);
    flags &= uint64_t(DebugModes::CompositionDebugMask);
    return FSRDCompFlags(flags);
}

using ModeNamePair = std::pair<const char*, uint64_t>;
constexpr auto kDebugModes = std::to_array<ModeNamePair>(
{
    { "None", (uint64_t) DebugModes::None },
    { "DebugOverview", (uint64_t) DebugModes::FfxDebug },

    { "DenoiserBypass", (uint64_t) DebugModes::DenoiserBypass },
    { "UpscalerBypass", (uint64_t) DebugModes::UpscalerBypass },
    { "DenoiserOutput", (uint64_t) DebugModes::DenoiserOutput },
    { "SkipSignal", (uint64_t) DebugModes::SkipSignal },

    { "RawColor", (uint64_t) DebugModes::RawColor },
    { "DlssBias", (uint64_t) DebugModes::DlssBias },
    { "DlssColorBeforeParticles", (uint64_t) DebugModes::DlssColorBeforeParticles },
    { "DlssColorBeforeTransparency", (uint64_t) DebugModes::DlssColorBeforeTransparency },
    { "DlssTransparencyLayer", (uint64_t) DebugModes::DlssTransparencyLayer },

    { "InMotionVectors", (uint64_t) DebugModes::InMotion },
    { "InNormals", (uint64_t) DebugModes::InNormals },
    { "InRoughness", (uint64_t) DebugModes::InRoughness },
    { "InSpecHitDist", (uint64_t) DebugModes::InSpecHitDist },
    { "InDiffAlbedo", (uint64_t) DebugModes::InDiffAlbedo },
    { "InSpecAlbedo", (uint64_t) DebugModes::InSpecAlbedo },

    { "OutRadiance", (uint64_t) DebugModes::OutRadiance },
    { "OutFusedAlbedo", (uint64_t) DebugModes::OutFusedAlbedo },
    { "OutLinearDepth", (uint64_t) DebugModes::OutLinearDepth },
    { "OutMotionVectors", (uint64_t) DebugModes::OutMotion },
    { "OutNormals", (uint64_t) DebugModes::OutNormals },
    { "OutSpecAlbedo", (uint64_t) DebugModes::OutSpecAlbedo },
    { "OutDiffAlbedo", (uint64_t) DebugModes::OutDiffAlbedo },
    { "OutDepthDelta", (uint64_t) DebugModes::OutDepthDelta },
    { "NormDepth", (uint64_t) DebugModes::NormDepth },

    { "AlbedoError", (uint64_t) DebugModes::AlbedoError },
    { "Correlation", (uint64_t) DebugModes::Correlation },

    { "FloorVariance", (uint64_t) DebugModes::FloorVariance },
    { "FloorColor", (uint64_t) DebugModes::FloorColor },

    { "Signal1", (uint64_t) DebugModes::Signal1 },
    { "Signal2", (uint64_t) DebugModes::Signal2 },
});

bool FSRDFeatureDx12::s_isHWDepth = false;
bool FSRDFeatureDx12::s_isRoughnessPacked = false;

FSRDFeatureDx12::FSRDFeatureDx12(uint32_t InHandleId, NVSDK_NGX_Parameter* InParameters) :
    FSR31FeatureDx12(InHandleId, InParameters),
    IFeature(InHandleId, SetParameters(InParameters)),
    _pDenoiserCtx(nullptr),
    _denoiserCtxDesc({}),
    _denoiserSettings({}),
    _convDesc({}),
    _signalFlags(0),
    _checkerboardSignalFlags(0)
{
    _moduleLoaded = FfxApiProxy::IsDenoiserReady();

    if (_moduleLoaded)
        LOG_INFO("amd_fidelityfx_denoiser_dx12.dll methods loaded!");
    else
        LOG_ERROR("can't load amd_fidelityfx_denoiser_dx12.dll methods!");
}

FSRDFeatureDx12::~FSRDFeatureDx12()
{
    if (State::Instance().isShuttingDown)
        return;

    DestroyDenoiserContext();
}

bool FSRDFeatureDx12::InitFSR3(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (FSR31FeatureDx12::InitFSR3(InParameters))
    {
        SetInit(false);

        LOG_DEBUG("FSR Ray Regeneration 1.2.0 Initializing");
        _name = OptiTexts::FSR_RR_Name;

        if (int value; InParameters->Get(NVSDK_NGX_Parameter_Use_HW_Depth, &value) == NVSDK_NGX_Result_Success)
            s_isHWDepth = value == NVSDK_NGX_DLSS_Depth_Type_HW;

        if (int value; InParameters->Get(NVSDK_NGX_Parameter_DLSS_Roughness_Mode, &value) == NVSDK_NGX_Result_Success)
            s_isRoughnessPacked = value == NVSDK_NGX_DLSS_Roughness_Mode_Packed;

        LOG_INFO("DLSSD Flags HWDepth: {} - IsRoughnessPacked: {}", s_isHWDepth, s_isRoughnessPacked);

        if (!CreateDenoiserContext())
            return false;

        LOG_INFO("FSR Ray Regeneration 1.2.0 Initialized");

        SetInit(true);
        return true;
    }

    return false;
}

bool FSRDFeatureDx12::CreateDenoiserContext()
{
    ScopedSkipSpoofing skipSpoofing {};
    auto& state = State::Instance();
    const auto& cfg = *Config::Instance();

    if (!QueryDenoiserVersions())
        return false;

    state.ffxDenoiserUpscalerVersion = Version();
    parse_version(state.ffxDenoiserVersionNames[cfg.FfxDenoiserIndex.value_or_default()]);

    // SDK 2.3.0: Build signalFlags from config instead of 'mode'
    // Default: IndirectSpecular signal (primary path for DLSS-RR interop)
    _signalFlags = cfg.FfxDenoiserSignalFlags.has_value()
        ? (uint32_t) cfg.FfxDenoiserSignalFlags.value()
        : (uint32_t) FFX_DENOISER_SIGNAL_INDIRECT_SPECULAR;

    // Optional dominant light visibility
    if (cfg.FfxDenoiserUseDominantLight.value_or_default())
        _signalFlags |= FFX_DENOISER_SIGNAL_DOMINANT_LIGHT_VISIBILITY; // renamed from FFX_DENOISER_ENABLE_DOMINANT_LIGHT

    _checkerboardSignalFlags = cfg.FfxDenoiserCheckerboardFlags.has_value()
        ? (uint32_t) cfg.FfxDenoiserCheckerboardFlags.value()
        : 0u;

    ffxOverrideVersion vidOverride =
    {
        .header = { .type = FFX_API_DESC_TYPE_OVERRIDE_VERSION },
        .versionId = state.ffxDenoiserVersionIds[cfg.FfxDenoiserIndex.value_or_default()]
    };

    // Backend descriptor
    ffxCreateBackendDX12Desc backendDesc =
    {
        .header =
        {
            .type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12,
            .pNext = &vidOverride.header
        },
        .device = Device
    };

    // SDK 2.3.0: 'mode' field removed; use signalFlags + checkerboardSignalFlags
    // SDK 2.3.0: 'fpMessage' field removed; use ffxConfigureDescGlobalDebug after context creation
    _denoiserCtxDesc =
    {
        .header =
        {
            .type = FFX_API_CREATE_CONTEXT_DESC_TYPE_DENOISER,
            .pNext = &backendDesc.header
        },
        .version = FFX_DENOISER_VERSION,
        .maxRenderSize = { RenderWidth(), RenderHeight() },
        .signalFlags = _signalFlags,
        .checkerboardSignalFlags = _checkerboardSignalFlags,
        .flags = 0
    };

#ifdef _DEBUG
    LOG_INFO("Debug/Validation enabled for FSR-RR denoiser.");
    _denoiserCtxDesc.flags |= FFX_DENOISER_ENABLE_DEBUGGING;
    // SDK 2.3.0: FFX_DENOISER_ENABLE_VALIDATION is a new optional validation flag
    if (cfg.FfxDenoiserEnableValidation.value_or_default())
        _denoiserCtxDesc.flags |= FFX_DENOISER_ENABLE_VALIDATION;
#endif

    // Create the denoiser context
    {
        ScopedSkipHeapCapture skipHeapCapture {};
        auto ret = FfxApiProxy::D3D12_CreateContext(&_pDenoiserCtx, &_denoiserCtxDesc.header, NULL);

        if (ret != FFX_API_RETURN_OK)
        {
            LOG_ERROR("_denoiserCtx error: {0}", FfxApiProxy::ReturnCodeToString(ret));
            return false;
        }
    }

    // SDK 2.3.0: Set up global debug logging via ffxConfigureDescGlobalDebug
    // (replaces fpMessage field that was removed from ffxCreateContextDescDenoiser)
    if (cfg.FfxDenoiserEnableValidation.value_or_default())
    {
        ffxConfigureDescGlobalDebug dbgDesc = {};
        dbgDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG;
        dbgDesc.fpMessage = [](uint32_t type, const wchar_t* msg) {
            // Route FSR-RR validation messages through OptiScaler logger
            LOG_DEBUG("[FSR-RR Validation] type={} msg={}", type, "");
        };
        FfxApiProxy::D3D12_Configure(&_pDenoiserCtx, &dbgDesc.header);
    }

    // Query default settings
    SetDefaultConfiguration();

    // Create DLSS-RR to FSR-RR input converter
    // SDK 2.3.0: Using single IndirectSpecular signal path (fused mode removed)
    FSRDConvShader = std::make_unique<FSRDPreprocessor_Dx12>("FSRD Converter", Device, false);

    if (!FSRDConvShader->IsInit())
        return false;

    if (!FSRDConvShader->SetMaxRenderSize(_denoiserCtxDesc.maxRenderSize.width, _denoiserCtxDesc.maxRenderSize.height))
        return false;

    // Populate debug mode state
    state.ffxDenoiserDebugModes.clear();
    state.ffxDenoiserDebugModeNames.clear();
    for (const auto& mode : kDebugModes)
    {
        state.ffxDenoiserDebugModes.push_back(mode.second);
        state.ffxDenoiserDebugModeNames.emplace(mode.second, mode.first);
    }

    return true;
}

bool FSRDFeatureDx12::QueryDenoiserVersions()
{
    ScopedSkipSpoofing skipSpoofing {};
    auto& state = State::Instance();

    // Get version count
    // SDK 2.3.0: ffxQueryDescDenoiserGetVersion is removed.
    // Version is now embedded in DLL name/provider. Use ffxQueryDescGetVersions instead.
    uint64_t versionCount = 0;
    ffxQueryDescGetVersions queryVersionsDesc =
    {
        .header = { .type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS },
        .createDescType = FFX_API_EFFECT_ID_DENOISER,
        .device = Device,
        .outputCount = &versionCount
    };
    FfxApiProxy::D3D12_Query(nullptr, &queryVersionsDesc.header);

    state.ffxDenoiserVersionIds.resize(versionCount);
    state.ffxDenoiserVersionNames.resize(versionCount);

    if (versionCount == 0)
    {
        LOG_ERROR("No FSR-RR denoisers were found.");
        return false;
    }
    else
        LOG_DEBUG("Found {} version(s) of FSR-RR (provider version embedded in DLL name)", versionCount);

    queryVersionsDesc.versionIds = state.ffxDenoiserVersionIds.data();
    queryVersionsDesc.versionNames = state.ffxDenoiserVersionNames.data();
    FfxApiProxy::D3D12_Query(nullptr, &queryVersionsDesc.header);

    return true;
}

void FSRDFeatureDx12::DestroyDenoiserContext()
{
    if (_pDenoiserCtx != nullptr)
        FfxApiProxy::D3D12_DestroyContext(&_pDenoiserCtx, nullptr);
}

void FSRDFeatureDx12::UpdateSize()
{
    const bool needsReInit =
        _denoiserCtxDesc.maxRenderSize.width != RenderWidth() ||
        _denoiserCtxDesc.maxRenderSize.height != RenderHeight();

    if (needsReInit)
    {
        LOG_INFO(
            "Reinitializing FSR-RR 1.2.0 for resolution change. "
            "Previous: {} x {}, New: {} x {}",
            _denoiserCtxDesc.maxRenderSize.width, _denoiserCtxDesc.maxRenderSize.height,
            RenderWidth(), RenderHeight());

        DestroyDenoiserContext();
        CreateDenoiserContext();
    }
}

bool FSRDFeatureDx12::Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!IsInited())
        return false;

    auto& state = State::Instance();
    auto& cfg = *Config::Instance();
    const auto& inParams = *InParameters;

    UpdateSize();

    const auto dbgMode = static_cast<DebugModes>(cfg.FfxDenoiserDebugMode.value_or_default());
    const bool isDebugVis = (uint32_t) dbgMode & (uint32_t) DebugModes::ConversionDebug;
    const bool isDebugComp = ((uint64_t) dbgMode & (uint64_t) DebugModes::CompositionDebug);
    const bool isFfxDebug = dbgMode == DebugModes::FfxDebug;
    const bool hasAnyDebug = (dbgMode != DebugModes::None);

    const bool isDenoiseBypassed = !isFfxDebug && !isDebugComp &&
        hasAnyDebug && dbgMode != DebugModes::DenoiserOutput && dbgMode != DebugModes::UpscalerBypass;

    const bool isUpscaleBypassed = hasAnyDebug && dbgMode != DebugModes::DenoiserBypass;

    if (!RCAS->IsInit())
        cfg.RcasEnabled.set_volatile_value(false);
    if (!OutputScaler->IsInit())
        cfg.OutputScalingEnabled.set_volatile_value(false);

    _isInReset = false;

    if (uint32_t value = 0; inParams.Get(NVSDK_NGX_Parameter_Reset, &value) == NVSDK_NGX_Result_Success)
        _isInReset = value > 0;

    // SDK 2.3.0: Per-signal dispatch descriptors replace fused Input1/2/4Signals structs.
    // Primary signal path: IndirectSpecular (DLSS-RR specular radiance interop)
    ffxDispatchDescDenoiserIndirectSpecular indSpecSignal = {};
    ffxDispatchDescDenoiser denoiserDesc = {};
    bool isDenoiserReady = false;

    if (!PrepareDenoiserInput(InCommandList, *InParameters, denoiserDesc, indSpecSignal))
        return false;

    if (!isDenoiseBypassed)
    {
        ffxDispatchDescDenoiserDebugView dispatchDebugView = {};

        if (isFfxDebug)
        {
            ffxDispatchDescHeader* signalHeader = denoiserDesc.header.pNext;
            if (signalHeader)
                signalHeader->pNext = &dispatchDebugView.header;

            ID3D12Resource* dstTex;
            TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_Output, dstTex);

            dispatchDebugView =
            {
                .header = { .type = FFX_API_DISPATCH_DESC_DEBUG_VIEW_TYPE_DENOISER },
                .output = ffxApiGetResourceDX12(dstTex, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS),
                .outputSize = { TargetWidth(), TargetHeight() },
                .mode = FFX_API_DENOISER_DEBUG_VIEW_MODE_OVERVIEW,
                .viewportIndex = 0
            };
        }

        isDenoiserReady = DispatchDenoiser(InCommandList, denoiserDesc);

        if (!isDenoiserReady)
            return false;

        FSRDCompDesc compDesc =
        {
            .DstTexSize = _convDesc.RenderSize,
            .CorrelationBias = cfg.FfxDenoiserCorrelationBias.value_or_default(),
            .Flags = (uint32_t) GetCompDebugFlags(dbgMode)
        };

        TryGetNGXVoidPointer(inParams, NVSDK_NGX_Parameter_Color, compDesc.InRawColor);
        TryGetNGXVoidPointer(inParams, NVSDK_NGX_Parameter_DLSSD_ColorBeforeParticles, compDesc.InColorBeforeParticles);

        if (!isFfxDebug && !FSRDConvShader->DispatchComposition(InCommandList, compDesc))
            return false;

        isDenoiserReady = true;
    }

    if (!isUpscaleBypassed)
    {
        ffxDispatchDescUpscale upscalerDesc = {};

        if (!PrepareUpscalerInput(InCommandList, inParams, upscalerDesc))
            return false;

        if (isDenoiserReady)
        {
            upscalerDesc.color = ffxApiGetResourceDX12(FSRDConvShader->GetCompositionOutput());
            // SDK 2.3.0: cameraFovAngleVertical still on upscalerDesc;
            // derive from proj matrix (denoiser no longer exposes it directly via deltaTime removal)
            upscalerDesc.cameraFovAngleVertical = GetVertFovFromProjectionMatrixRad(_projMatrix);
            // SDK 2.3.0: deltaTime removed from denoiserDesc; use upscaler's own frameTimeDelta
        }

        FSR31FeatureDx12::SetConfigurableBarriers(InCommandList);

        bool isUpscalerReady = DispatchUpscaler(InCommandList, upscalerDesc);

        if (isUpscalerReady)
            PostProcess(InCommandList, inParams);

        FSR31FeatureDx12::ResetConfigurableBarriers(InCommandList);
    }
    else if (!isFfxDebug)
    {
        ID3D12Resource* srcTex = nullptr;

        if (dbgMode == DebugModes::DlssColorBeforeParticles)
            TryGetNGXVoidPointer(inParams, NVSDK_NGX_Parameter_DLSSD_ColorBeforeParticles, srcTex);
        else if (dbgMode == DebugModes::DlssColorBeforeTransparency)
            TryGetNGXVoidPointer(inParams, NVSDK_NGX_Parameter_DLSSD_ColorBeforeTransparency, srcTex);
        else if (dbgMode == DebugModes::DlssTransparencyLayer)
            TryGetNGXVoidPointer(inParams, NVSDK_NGX_Parameter_DLSS_TransparencyLayer, srcTex);
        else if (dbgMode == DebugModes::DlssBias)
            TryGetNGXVoidPointer(inParams, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, srcTex);
        else if (dbgMode == DebugModes::RawColor)
            TryGetNGXVoidPointer(inParams, NVSDK_NGX_Parameter_Color, srcTex);
        else if (isDebugVis)
            srcTex = GetD3D12ResFromFFX(indSpecSignal.specularRadiance.input);
        else
            srcTex = FSRDConvShader->GetCompositionOutput();

        ID3D12Resource* dstTex;

        if (!srcTex || !TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_Output, dstTex))
        {
            _frameCount++;
            return true;
        }

        FSRDConvShader->Blit(InCommandList, srcTex, dstTex);
    }

    _frameCount++;
    return isDenoiserReady || isDenoiseBypassed;
}

bool FSRDFeatureDx12::PrepareDenoiserInput(ID3D12GraphicsCommandList* InCommandList,
                                           const NVSDK_NGX_Parameter& inParams,
                                           ffxDispatchDescDenoiser& dispatchDesc,
                                           ffxDispatchDescDenoiserIndirectSpecular& signalDesc)
{
    const auto& cfg = *Config::Instance();
    const auto& slData = State::Instance().slLastConstants;

    if (!PrepareDenoiseConvInput(inParams))
        return false;

    if (!ConvertDenoiserBuffers(InCommandList))
        return false;

    // Camera position from inverse view matrix column 3
    const XMFLOAT3 camPos = GetFloat3Column(_invViewMatrix, 3);

    // SDK 2.3.0: Camera component fields removed.
    // Use view and projection matrices directly as FfxApiFloatMatrix44.
    FfxApiFloatMatrix44 viewMat44 = {};
    FfxApiFloatMatrix44 projMat44 = {};

    // FSR-RR expects left-handed view matrix. Flip Z column if right-handed.
    XMMATRIX dispatchViewMatrix = _viewMatrix;
    if (_isRightHanded)
    {
        // Negate the Z basis (column 2 of view matrix in row-major storage)
        for (int row = 0; row < 4; row++)
            dispatchViewMatrix.r[row].m128_f32[2] *= -1.0f;
    }

    StoreFfxMatrix(dispatchViewMatrix, viewMat44);
    StoreFfxMatrix(_projMatrix, projMat44);

    // SDK 2.3.0: Build base dispatch descriptor with matrix API
    dispatchDesc =
    {
        .header =
        {
            .type = FFX_API_DISPATCH_DESC_TYPE_DENOISER,
            // Chain the per-signal descriptor (IndirectSpecular)
            .pNext = &signalDesc.header
        },
        .commandList = InCommandList,
        // SDK 2.3.0: view + projection replace all camera*** fields
        .view = viewMat44,
        .projection = projMat44,
        .motionVectorScale = { 1.0f, 1.0f, 1.0f },
        // Camera movement delta (previous - current position)
        .cameraPositionDelta = {
            (_lastCamPos.x - camPos.x),
            (_lastCamPos.y - camPos.y),
            (_lastCamPos.z - camPos.z)
        },
        // SDK 2.3.0: linearDepth is now SIGNED (not absolute)
        // The converter already outputs signed linear depth, no fabsf needed.
        // motionVectors.z is also signed delta now.
        .renderSize = { RenderWidth(), RenderHeight() },
        .frameIndex = (uint32_t) _frameCount,
        .flags = FFX_DENOISER_DISPATCH_NON_GAMMA_ALBEDO,
        // SDK 2.3.0: linearDepthBounds passthrough (optional, set to near/far)
        .linearDepthBounds = { _convDesc.NearPlane, _convDesc.FarPlane },
        // SDK 2.3.0: deltaTime field removed entirely
    };

    // Populate signal resources and link header
    FSRDConvShader->GetSignal(signalDesc, dispatchDesc);

    if (_isInReset)
        dispatchDesc.flags |= FFX_DENOISER_DISPATCH_RESET;

    _lastCamPos = camPos;

    // Motion Vector Scaling
    float MVScaleX = 1.0f, MVScaleY = 1.0f;
    if (inParams.Get(NVSDK_NGX_Parameter_MV_Scale_X, &MVScaleX) == NVSDK_NGX_Result_Success &&
        inParams.Get(NVSDK_NGX_Parameter_MV_Scale_Y, &MVScaleY) == NVSDK_NGX_Result_Success)
    {
        dispatchDesc.motionVectorScale.x = MVScaleX / dispatchDesc.renderSize.width;
        dispatchDesc.motionVectorScale.y = MVScaleY / dispatchDesc.renderSize.height;
        // SDK 2.3.0: motionVectorScale.z is now signed linear depth delta scaling factor
        // (not absolute). Keep as 1.0f default (converter handles the sign).
        dispatchDesc.motionVectorScale.z = 1.0f;
    }

    float jitterX = 0.0f, jitterY = 0.0f;
    inParams.Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &jitterX);
    inParams.Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &jitterY);

    // Convert pixel-space jitter to NDC
    dispatchDesc.jitterOffsets.x = 2.0f * (jitterX / (float) RenderWidth());
    dispatchDesc.jitterOffsets.y = -2.0f * (jitterY / (float) RenderHeight());

    LOG_DEBUG("Jitter NDC [{:.6f}, {:.6f}]", dispatchDesc.jitterOffsets.x, dispatchDesc.jitterOffsets.y);

    return true;
}

bool FSRDFeatureDx12::PrepareDenoiseConvInput(const NVSDK_NGX_Parameter& inParams)
{
    const auto& slData = State::Instance().slLastConstants;

    bool isReady = true;

    if (!TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_Color, _convDesc.Resources.InColor))
        isReady = false;
    if (!TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_MotionVectors, _convDesc.Resources.InMotionVectors))
        isReady = false;
    if (!TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_Depth, _convDesc.Resources.InDepth) && LowResMV())
        isReady = false;

    if (!TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_GBuffer_Normals, _convDesc.Resources.InNormals))
        isReady = false;

    if (!TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_GBuffer_Roughness, _convDesc.Resources.InRoughness) &&
        !s_isRoughnessPacked)
    {
        LOG_WARN("Expected unpacked roughness buffer from DLSS-RR. Defaulting to packed roughness...");
        s_isRoughnessPacked = true;
    }

    if (!TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_DiffuseAlbedo, _convDesc.Resources.InDiffAlbedo))
        isReady = false;

    if (!TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_SpecularAlbedo, _convDesc.Resources.InSpecAlbedo))
        isReady = false;

    TryGetNGXVoidPointer(inParams, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, _convDesc.Resources.InBiasMask);
    TryGetLoggedResource(inParams, NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance, _convDesc.Resources.InSpecHitDist);

    // World to view/camera space matrix
    _prevViewMatrix = _viewMatrix;
    _viewMatrix = {};

    if (!TryGetNGXMatrix(inParams, NVSDK_NGX_Parameter_DLSS_WORLD_TO_VIEW_MATRIX, _viewMatrix))
    {
        if (StreamlineHooks::isSetConstantsHooked())
        {
            SetColumn(XMLoadFloat3((XMFLOAT3*) &slData.cameraRight), 0, _invViewMatrix);
            SetColumn(XMLoadFloat3((XMFLOAT3*) &slData.cameraUp), 1, _invViewMatrix);
            SetColumn(XMLoadFloat3((XMFLOAT3*) &slData.cameraFwd), 2, _invViewMatrix);
            SetColumn(XMLoadFloat3((XMFLOAT3*) &slData.cameraPos), 3, _invViewMatrix);
            _invViewMatrix.r[3].m128_f32[3] = 1.0f;
            _viewMatrix = XMMatrixInverse(nullptr, _invViewMatrix);
        }
        else
        {
            LOG_ERROR("View matrix missing! Denoiser not ready.");
            isReady = false;
        }
    }
    else
    {
        _invViewMatrix = XMMatrixInverse(nullptr, _viewMatrix);
    }

    _projMatrix = {};

    if (!TryGetNGXMatrix(inParams, NVSDK_NGX_Parameter_DLSS_VIEW_TO_CLIP_MATRIX, _projMatrix))
    {
        if (StreamlineHooks::isSetConstantsHooked())
        {
            if (slData.cameraFOV != sl::INVALID_FLOAT && slData.cameraNear != slData.cameraFar)
            {
                const float fov = (slData.cameraFOV < 4.0f) ? slData.cameraFOV : GetRadiansFromDeg(slData.cameraFOV);
                const float nearPlane = slData.cameraNear;
                const float farPlane = slData.cameraFar;
                _isRightHanded = slData.cameraViewToClip[2].w < 0.0f;

                if (_isRightHanded)
                    _projMatrix = XMMatrixPerspectiveFovRH(fov, slData.cameraAspectRatio, nearPlane, farPlane);
                else
                    _projMatrix = XMMatrixPerspectiveFovLH(fov, slData.cameraAspectRatio, nearPlane, farPlane);

                _projMatrix = XMMatrixTranspose(_projMatrix);
            }
        }
        else
        {
            LOG_ERROR("Projection matrix missing! Denoiser not ready.");
            isReady = false;
        }
    }

    return isReady;
}

bool FSRDFeatureDx12::ConvertDenoiserBuffers(ID3D12GraphicsCommandList* InCommandList)
{
    const uint32_t dbgMode = (uint32_t) Config::Instance()->FfxDenoiserDebugMode.value_or_default();
    const auto& cfg = *Config::Instance();

    _convDesc.RenderSize =
    {
        (float) RenderWidth(), (float) RenderHeight(),
        1.0f / (float) RenderWidth(), 1.0f / (float) RenderHeight()
    };
    _convDesc.Flags = (uint32_t) FSRDConvFlags::NonGammaAlbedo | (dbgMode & (uint32_t) FSRDConvFlags::DebugModeMask);
    _convDesc.FloorIsolation = cfg.FfxDenoiserFloorIsolation.value_or_default();

    if (s_isRoughnessPacked)
        _convDesc.Flags |= (uint32_t) FSRDConvFlags::IsRoughnessPacked;

    XMStoreFloat4x4(&_convDesc.InvViewMatrix, XMMatrixTranspose(_invViewMatrix));

    const XMMATRIX invProjMatrix = XMMatrixInverse(nullptr, _projMatrix);
    XMStoreFloat4x4(&_convDesc.InvProjMatrix, XMMatrixTranspose(invProjMatrix));

    XMStoreFloat4x4(&_convDesc.PrevViewMatrix, XMMatrixTranspose(_prevViewMatrix));

    const ViewPlanes planes = GetViewPlanes(_projMatrix, DepthInverted());
    _convDesc.NearPlane = planes.nearPlane;
    _convDesc.FarPlane = planes.farPlane;
    _isRightHanded = planes.isRightHanded;

    if (!s_isHWDepth)
        _convDesc.Flags |= (uint32_t) FSRDConvFlags::IsDepthLinear;

    LOG_DEBUG("Dispatching FSRD Input Converter");

    if (!FSRDConvShader->DispatchConversion(InCommandList, _convDesc))
        return false;

    return true;
}

static bool TryUpdateOption(const CustomOptional<float>& cfgValue, float& currentValue)
{
    if (cfgValue.value_or_default() != currentValue)
    {
        currentValue = cfgValue.value_or_default();
        return true;
    }
    else
        return false;
}

bool FSRDFeatureDx12::DispatchDenoiser(ID3D12GraphicsCommandList* InCommandList,
                                       const ffxDispatchDescDenoiser& dispatchDesc)
{
    auto& state = State::Instance();
    const auto& cfg = *Config::Instance();

    if (TryUpdateOption(cfg.FfxDenoiserDisocThreshold, _denoiserSettings.m_DisocclusionThreshold))
        ApplyConfiguration(FFX_API_CONFIGURE_DENOISER_KEY_DISOCCLUSION_THRESHOLD);
    if (TryUpdateOption(cfg.FfxDenoiserCrossBlNormStr, _denoiserSettings.m_CrossBilateralNormalStrength))
        ApplyConfiguration(FFX_API_CONFIGURE_DENOISER_KEY_CROSS_BILATERAL_NORMAL_STRENGTH);
    if (TryUpdateOption(cfg.FfxDenoiserStabilityBias, _denoiserSettings.m_StabilityBias))
        ApplyConfiguration(FFX_API_CONFIGURE_DENOISER_KEY_STABILITY_BIAS);
    if (TryUpdateOption(cfg.FfxDenoiserMaxRadiance, _denoiserSettings.m_MaxRadiance))
        ApplyConfiguration(FFX_API_CONFIGURE_DENOISER_KEY_MAX_RADIANCE);
    if (TryUpdateOption(cfg.FfxDenoiserRadianceClip, _denoiserSettings.m_RadianceClipStdK))
        ApplyConfiguration(FFX_API_CONFIGURE_DENOISER_KEY_RADIANCE_CLIP_STD_K);
    if (TryUpdateOption(cfg.FfxDenoiserGaussKernRelax, _denoiserSettings.m_GaussianKernelRelaxation))
        ApplyConfiguration(FFX_API_CONFIGURE_DENOISER_KEY_GAUSSIAN_KERNEL_RELAXATION);

    // SDK 2.3.0: FFX_API_CONFIGURE_DENOISER_KEY_DEBUG_VIEW_LINEAR_DEPTH_BOUNDS is new
    // It is applied via ffxConfigure if the INI option is set
    if (cfg.FfxDenoiserDebugLinearDepthBounds.has_value() && cfg.FfxDenoiserDebugLinearDepthBounds.value())
    {
        ffxConfigureDescDenoiserKeyValue linearDepthBoundsCfg =
        {
            .header = { .type = FFX_API_CONFIGURE_DESC_TYPE_DENOISER_KEYVALUE },
            .key = (uint64_t) FFX_API_CONFIGURE_DENOISER_KEY_DEBUG_VIEW_LINEAR_DEPTH_BOUNDS,
            .count = 1u,
            .data = nullptr
        };
        FfxApiProxy::D3D12_Configure(&_pDenoiserCtx, &linearDepthBoundsCfg.header);
    }

    LOG_DEBUG("Dispatching FSR-RR 1.2.0...");
    const ffxReturnCode_t result = FfxApiProxy::D3D12_Dispatch(&_pDenoiserCtx, &dispatchDesc.header);

    if (result != FFX_API_RETURN_OK)
    {
        LOG_ERROR("_dispatch error: {0}", FfxApiProxy::ReturnCodeToString(result));

        if (result == FFX_API_RETURN_ERROR_RUNTIME_ERROR)
        {
            LOG_WARN("Trying to recover by recreating the feature");
            state.changeBackend[Handle()->Id] = true;
        }

        return false;
    }

    return true;
}

void FSRDFeatureDx12::SetDefaultConfiguration()
{
    for (int i = 0; i < DenoiserConfiguration::kCount; i++)
        SetDefaultConfiguration(DenoiserConfiguration::GetIndexKey(i));
}

ffxReturnCode_t FSRDFeatureDx12::SetDefaultConfiguration(FfxApiConfigureDenoiserKey key)
{
    ffxQueryDescDenoiserGetDefaultKeyValue queryDesc =
    {
        .header = { .type = FFX_API_QUERY_DESC_TYPE_DENOISER_GET_DEFAULT_KEYVALUE },
        .key = (uint64_t) key,
        .count = 1u,
        .data = &_denoiserSettings.GetMember(key)
    };

    const ffxReturnCode_t code = FfxApiProxy::D3D12_Query(&_pDenoiserCtx, &queryDesc.header);
    return code;
}

ffxReturnCode_t FSRDFeatureDx12::ApplyConfiguration(FfxApiConfigureDenoiserKey key)
{
    ffxQueryDescDenoiserGetDefaultKeyValue configureDesc =
    {
        .header = { .type = FFX_API_CONFIGURE_DESC_TYPE_DENOISER_KEYVALUE },
        .key = (uint64_t) key,
        .count = 1u,
        .data = &_denoiserSettings.GetMember(key)
    };

    const ffxReturnCode_t code = FfxApiProxy::D3D12_Configure(&_pDenoiserCtx, &configureDesc.header);
    return code;
}
