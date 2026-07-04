#pragma once
#include "FSR31Feature_Dx12.h"
#include "shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.h"
#include <DirectXMath.h>

/**
 * @brief Unified denoiser-upscaler utilising AMD FSR Ray Regeneration 1.2.0 and Super Resolution
 * with DLSS-RR inputs. Extends FSR 3.1+ upscaler implementation.
 * Updated for FidelityFX SDK 2.3.0 (FSR RR 1.2.0) breaking API changes:
 *   - Removed Fuse mode API (FfxApiDenoiserMode, mode field, fused dispatch structs)
 *   - Replaced camera component fields with view/projection matrices
 *   - Switched to signed linearDepth convention
 *   - Added per-signal dispatch descriptors (AO, DirectDiffuse, DirectSpecular,
 *     IndirectDiffuse, IndirectSpecular, SpecularOcclusion, DominantLight)
 *   - Added signalFlags and checkerboardSignalFlags to context creation
 *   - Replaced fpMessage with ffxConfigureDescGlobalDebug
 *   - Added linearDepthBounds passthrough field
 *   - Added checkerboardOrigin to FfxApiDenoiserSignal
 */
class FSRDFeatureDx12 : public FSR31FeatureDx12
{
  public:
    using FSRDConvDesc = FSRDPreprocessor_Dx12::ConversionDesc;

    FSRDFeatureDx12(uint32_t InHandleId, NVSDK_NGX_Parameter* InParameters);

    ~FSRDFeatureDx12();

    feature_version Version() override { return FSR31FeatureDx12::Version(); }

    std::string Name() const override { return FSR31FeatureDx12::Name(); }

    bool Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;

  private:

    union DenoiserConfiguration
    {
        static constexpr uint32_t kCount = FFX_API_CONFIGURE_DENOISER_KEY_DISOCCLUSION_THRESHOLD;

        // Ordered by FfxApiConfigureDenoiserKey
        struct
        {
            float m_CrossBilateralNormalStrength;
            float m_StabilityBias;
            float m_MaxRadiance;
            float m_RadianceClipStdK;
            float m_GaussianKernelRelaxation;
            float m_DisocclusionThreshold;
        };

        float AsArray[kCount];

        static int GetKeyIndex(FfxApiConfigureDenoiserKey key)
        {
            return std::clamp((int) key - 1, 0, (int) DenoiserConfiguration::kCount);
        }

        static FfxApiConfigureDenoiserKey GetIndexKey(int index)
        {
            index = std::clamp(index, 0, (int) DenoiserConfiguration::kCount);
            return static_cast<FfxApiConfigureDenoiserKey>(index);
        }

        float& GetMember(int index) { return AsArray[index]; }

        float& GetMember(FfxApiConfigureDenoiserKey key) { return AsArray[GetKeyIndex(key)]; }
    };

    ffxContext _pDenoiserCtx;
    ffxCreateContextDescDenoiser _denoiserCtxDesc;
    DenoiserConfiguration _denoiserSettings;

    // SDK 2.3.0: signal and checkerboard flags replace 'mode'
    uint32_t _signalFlags;
    uint32_t _checkerboardSignalFlags;

    static bool s_isHWDepth;
    static bool s_isRoughnessPacked;

    FSRDConvDesc _convDesc;
    DirectX::XMFLOAT3 _lastCamPos;

    // Matrices
    DirectX::XMMATRIX _invViewMatrix;
    DirectX::XMMATRIX _viewMatrix;
    DirectX::XMMATRIX _prevViewMatrix;
    DirectX::XMMATRIX _projMatrix;
    bool _isRightHanded;

    std::unique_ptr<FSRDPreprocessor_Dx12> FSRDConvShader;

    bool InitFSR3(const NVSDK_NGX_Parameter* InParameters) override;

    bool CreateDenoiserContext();

    bool QueryDenoiserVersions();

    void DestroyDenoiserContext();

    void UpdateSize();

    /**
     * @brief Generates FFX denoiser configuration and per-signal dispatch descriptors from
     * DLSS-RR inputs and NGX configurations (SDK 2.3.0 per-signal API).
     */
    bool PrepareDenoiserInput(ID3D12GraphicsCommandList* InCommandList,
                              const NVSDK_NGX_Parameter& ngxParams,
                              ffxDispatchDescDenoiser& dispatchDesc,
                              ffxDispatchDescDenoiserIndirectSpecular& signalDesc);

    /**
     * @brief Retrieves DLSS-RR inputs to populate the inputs for the interop layer.
     */
    bool PrepareDenoiseConvInput(const NVSDK_NGX_Parameter& inParams);

    /**
     * @brief Converts previously retrieved DLSS-RR resources into FSR-RR inputs.
     */
    bool ConvertDenoiserBuffers(ID3D12GraphicsCommandList* InCommandList);

    /**
     * @brief Dispatches FSR-RR denoiser converted inputs. Runs before upscaler.
     */
    bool DispatchDenoiser(ID3D12GraphicsCommandList* InCommandList, const ffxDispatchDescDenoiser& dispatchDesc);

    void SetDefaultConfiguration();

    ffxReturnCode_t SetDefaultConfiguration(FfxApiConfigureDenoiserKey key);

    ffxReturnCode_t ApplyConfiguration(FfxApiConfigureDenoiserKey key);
};
