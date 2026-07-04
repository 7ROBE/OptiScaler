# FSR Ray Regeneration 1.2.0 / FidelityFX SDK 2.3.0 Migration

## OptiScaler 0.9.4 — FSR RR 1.2.0 + SDK 2.3.0

### Version Bump
- `resource.h`: `VER_HOTFIX_VERSION` 0 → **4** (0.9.0 → **0.9.4**)

### Breaking API Changes Applied (SDK 2.3.0)

#### Removed Fields / Structs
| Removed | Replacement |
|---|---|
| `FfxApiDenoiserSignal::_reserved` | Field removed, no action needed |
| `ffxDispatchDescDenoiser::deltaTime` | Removed entirely; upscaler derives its own frame time |
| `ffxQueryDescDenoiserGetVersion` | Version now embedded in DLL/provider name |
| `FfxApiDenoiserMode` | Removed; replaced by `signalFlags` |
| `ffxCreateContextDescDenoiser::mode` | Replaced by `.signalFlags` + `.checkerboardSignalFlags` |
| `ffxCreateContextDescDenoiser::fpMessage` | Replaced by `ffxConfigureDescGlobalDebug` post-creation |
| `ffxDispatchDescDenoiserInput1Signal` | Replaced by `ffxDispatchDescDenoiserIndirectSpecular` |
| `ffxDispatchDescDenoiserInput2Signals` | Replaced by per-signal dispatch descriptors |
| `ffxDispatchDescDenoiserInput4Signals` | Replaced by per-signal dispatch descriptors |
| `ffxQueryDescDenoiserGetGPUMemoryUsage::mode` | Replaced by `::signalFlags` |
| `FFX_DENOISER_ENABLE_DOMINANT_LIGHT` | Renamed to `FFX_DENOISER_SIGNAL_DOMINANT_LIGHT_VISIBILITY` |
| `ffxDispatchDescDenoiserInputDominantLight` | Renamed to `ffxDispatchDescDenoiserDominantLight` |

#### New APIs Added
- `ffxCreateContextDescDenoiser::signalFlags` — bitmask of active signal types
- `ffxCreateContextDescDenoiser::checkerboardSignalFlags` — checkerboard rendering support
- `ffxDispatchDescDenoiser::view` — full 4x4 view matrix (replaces cameraRight/Up/Forward/Near/Far/AspectRatio/FOV)
- `ffxDispatchDescDenoiser::projection` — full 4x4 projection matrix
- `ffxDispatchDescDenoiser::linearDepthBounds` — passthrough near/far bounds
- `FfxApiDenoiserSignal::checkerboardOrigin` — checkerboard rendering origin per signal
- `FFX_DENOISER_ENABLE_VALIDATION` — new debug validation flag on context
- `FFX_API_CONFIGURE_DENOISER_KEY_DEBUG_VIEW_LINEAR_DEPTH_BOUNDS` — new configure key
- New per-signal dispatch types: `ffxDispatchDescDenoiserIndirectSpecular`, `ffxDispatchDescDenoiserIndirectDiffuse`, `ffxDispatchDescDenoiserDirectDiffuse`, `ffxDispatchDescDenoiserDirectSpecular`, `ffxDispatchDescDenoiserAmbientOcclusion`, `ffxDispatchDescDenoiserSpecularOcclusion`, `ffxDispatchDescDenoiserDominantLight`

#### Sign Convention Changes
- `ffxDispatchDescDenoiser::linearDepth` — now **signed** linear depth (was absolute)
- `ffxDispatchDescDenoiser::motionVectors::z` — now **signed** linear depth delta (was absolute)
- `ffxDispatchDescDenoiser::motionVectorScale::z` — now **signed** scaling factor (was absolute)

### Files Changed
- `OptiScaler/resource.h` — version 0.9.0 → 0.9.4
- `OptiScaler/upscalers/fsr31/FSRDFeature_Dx12.h` — removed `_isMode2`, added `_signalFlags`, `_checkerboardSignalFlags`; updated `PrepareDenoiserInput` signature to use per-signal struct
- `OptiScaler/upscalers/fsr31/FSRDFeature_Dx12.cpp` — full migration to SDK 2.3.0 API

### Notes
- The `external/FidelityFX-SDK-v2` submodule must be checked out at tag `v2.3.0` for the new header definitions to be present.
- `Config.h` / `Config.cpp` should add: `FfxDenoiserSignalFlags`, `FfxDenoiserCheckerboardFlags`, `FfxDenoiserEnableValidation`, `FfxDenoiserDebugLinearDepthBounds`, `FfxDenoiserUseDominantLight` options.
