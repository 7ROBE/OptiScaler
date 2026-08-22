#include "pch.h"
#include "FeatureProvider_Dx12.h"

#include "Util.h"
#include "Config.h"

#include "NVNGX_Parameter.h"

#include "upscalers/dlss/DLSSFeature_Dx12.h"
#include "upscalers/dlssd/DLSSDFeature_Dx12.h"
#include "upscalers/fsr2/FSR2Feature_Dx12.h"
#include "upscalers/fsr2_212/FSR2Feature_Dx12_212.h"
<<<<<<< HEAD
#include "upscalers/fsr31/FSR31Feature_Dx12.h"
#include "upscalers/fsr31/FSRDFeature_Dx12.h"
=======
#include "upscalers/ffx/FFXFeature_Dx12.h"
#include "upscalers/fsr31/FSRDFeature_Dx12.h"
>>>>>>> upstream/master
#include "upscalers/xess/XeSSFeature_Dx12.h"
#include "FeatureProvider_Dx11.h"
#include <misc/IdentifyGpu.h>

<<<<<<< HEAD
bool FeatureProvider_Dx12::GetFeature(std::string_view upscalerName, UINT handleId, NVSDK_NGX_Feature featureID,
                                      NVSDK_NGX_Parameter* parameters, std::unique_ptr<IFeature_Dx12>* feature)
{
    State& state = State::Instance();
    Config& cfg = *Config::Instance();
=======
bool FeatureProvider_Dx12::GetFeature(Upscaler upscaler, UINT handleId, NVSDK_NGX_Parameter* parameters,
                                      std::unique_ptr<IFeature_Dx12>* feature)
{
    State& state = State::Instance();
    Config& cfg = *Config::Instance();
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
>>>>>>> upstream/master
    ScopedSkipHeapCapture skipHeapCapture {};
    std::string_view config_upscaler(upscalerName);
    bool loaded = false;

<<<<<<< HEAD
    // Feature type is fixed during NVSDK_NGX_D3D12_CreateFeature. 
    // To change the type, the feature has to be released by the application first.
    if (featureID == NVSDK_NGX_Feature_SuperSampling)
    {
        if (upscalerName == "xess")
        {
            *feature = std::make_unique<XeSSFeatureDx12>(handleId, parameters);
        }
        else if (upscalerName == "fsr21")
        {
            *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
        }
        else if (upscalerName == "fsr22")
        {
            *feature = std::make_unique<FSR2FeatureDx12>(handleId, parameters);
        }
        else if (upscalerName == OptiKeys::FSR31)
        {
            *feature = std::make_unique<FSR31FeatureDx12>(handleId, parameters);
        }
        else if (cfg.DLSSEnabled.value_or_default())
        {
            if (upscalerName == "dlss" && state.NVNGX_DLSS_Path.has_value())
            {
                *feature = std::make_unique<DLSSFeatureDx12>(handleId, parameters);
            }
            else
            {
                *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
                config_upscaler = "fsr21";
            }
=======
    switch (upscaler)
    {
    case Upscaler::XeSS:
        *feature = std::make_unique<XeSSFeatureDx12>(handleId, parameters);
        break;

    case Upscaler::FSR21:
        *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
        break;

    case Upscaler::FSR22:
        *feature = std::make_unique<FSR2FeatureDx12>(handleId, parameters);
        break;

    case Upscaler::FFX:
        *feature = std::make_unique<FFXFeatureDx12>(handleId, parameters);
        break;

    case Upscaler::DLSS:
        if (primaryGpu.dlssCapable && state.NVNGX_DLSS_Path.has_value())
        {
            *feature = std::make_unique<DLSSFeatureDx12>(handleId, parameters);
            break;
>>>>>>> upstream/master
        }
        else
        {
            *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
<<<<<<< HEAD
            config_upscaler = "fsr21";
        }

        loaded = (*feature)->ModuleLoaded();

        if (!loaded)
        {
            *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
            config_upscaler = "fsr21";
            loaded = true; // Assuming the fallback always loads successfully
        }
    }
    else if (featureID == NVSDK_NGX_Feature_RayReconstruction)
    {
        if (!upscalerName.starts_with("dlss"))
            upscalerName = OptiKeys::FSR_RR;

        if (upscalerName == OptiKeys::FSR_RR)
        {
            if (FfxApiProxy::IsDenoiserReady())
                *feature = std::make_unique<FSRDFeatureDx12>(handleId, parameters);
            else
                return false;
        }
        else if (cfg.DLSSEnabled.value_or_default())
        {
            if (upscalerName == "dlssd" && state.NVNGX_DLSSD_Path.has_value())
                *feature = std::make_unique<DLSSDFeatureDx12>(handleId, parameters);
            else
                return false;
        }
        else
            return false;

        loaded = (*feature)->ModuleLoaded();
    }

    // Handle display name normalization for DLSSD
    if (config_upscaler == "dlssd")
        config_upscaler = "dlss";

    cfg.Dx12Upscaler = std::string(config_upscaler);
=======
            upscaler = Upscaler::FSR21;
            break;
        }

    case Upscaler::FSRD:
        if (FfxApiProxy::IsDenoiserReady())
            *feature = std::make_unique<FSRDFeatureDx12>(handleId, parameters);
        else
        {
            *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
            upscaler = Upscaler::FSR21;
        }
        break;

    case Upscaler::DLSSD:
        if (primaryGpu.dlssCapable && state.NVNGX_DLSSD_Path.has_value())
        {
            *feature = std::make_unique<DLSSDFeatureDx12>(handleId, parameters);
            break;
        }
        else
        {
            *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
            upscaler = Upscaler::FSR21;
            break;
        }

    default:
        *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
        upscaler = Upscaler::FSR21;
        break;
    }

    bool loaded = (*feature)->ModuleLoaded();

    if (!loaded)
    {
        // Fail after the constructor
        ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "Falling back to FSR 2.1.2" });
        *feature = std::make_unique<FSR2FeatureDx12_212>(handleId, parameters);
        upscaler = Upscaler::FSR21;
        loaded = true; // Assuming the fallback always loads successfully
    }

    // DLSSD is stored in the config as DLSS
    if (upscaler == Upscaler::DLSSD)
        upscaler = Upscaler::DLSS;

    cfg.Dx12Upscaler = upscaler;
>>>>>>> upstream/master

    return loaded;
}

<<<<<<< HEAD
bool FeatureProvider_Dx12::ChangeFeature(std::string_view upscalerName, ID3D12Device* device,
                                         ID3D12GraphicsCommandList* cmdList, UINT handleId,
                                         NVSDK_NGX_Parameter* parameters, ContextData<IFeature_Dx12>* contextData)
=======
bool FeatureProvider_Dx12::ChangeFeature(Upscaler upscaler, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                                         UINT handleId, NVSDK_NGX_Parameter* parameters,
                                         ContextData<IFeature_Dx12>* contextData)
>>>>>>> upstream/master
{
    State& state = State::Instance();
    Config& cfg = *Config::Instance();

    if (!state.changeBackend[handleId])
        return false;

<<<<<<< HEAD
    const bool isDlssBeingEnabled = !cfg.DLSSEnabled.value_or_default() && state.newBackend == "dlss";

    // If no name or if dlss is being enabled use the configured upscaler name
    if (state.newBackend == "" || isDlssBeingEnabled)
        state.newBackend = cfg.Dx12Upscaler.value_or_default();

    if (contextData->featureID == NVSDK_NGX_Feature_RayReconstruction)
    {
        // RR features are not allowed to change. They can only init/reinit.
        if (state.newBackend != contextData->featureKey)
            state.newBackend = contextData->featureKey;
    }
=======
    const bool dlssOnNonCapable = !IdentifyGpu::getPrimaryGpu().dlssCapable && state.newBackend == Upscaler::DLSS;
    if (state.newBackend == Upscaler::Reset || dlssOnNonCapable)
        state.newBackend = cfg.Dx12Upscaler.value_or_default();
>>>>>>> upstream/master

    contextData->changeBackendCounter++;

    LOG_INFO("changeBackend is true, counter: {0}", contextData->changeBackendCounter);

    // first release everything
    if (contextData->changeBackendCounter == 1)
    {
<<<<<<< HEAD
        if (state.currentFG != nullptr && state.currentFG->IsActive() && state.activeFgInput == FGInput::Upscaler)
        {
            state.currentFG->DestroyFGContext();
            state.FGchanged = true;
            state.ClearCapturedHudlesses = true;
=======
        if (state.currentFG != nullptr && state.activeFgInput == FGInput::Upscaler)
        {
            state.fgChanged = true;
            state.clearCapturedHudlesses = true;
>>>>>>> upstream/master
        }

        if (contextData->feature != nullptr)
        {
<<<<<<< HEAD
            LOG_INFO("changing backend to {}", state.newBackend);

            auto* dc = contextData->feature.get();
            // Use given params if using DLSS passthrough
            const std::string_view backend = state.newBackend;
            const bool isPassthrough = backend == "dlssd" || backend == "dlss";

            contextData->createParams = isPassthrough ? parameters : GetNGXParameters("OptiDx12", false);
=======
            LOG_INFO("changing backend to {}", UpscalerDisplayName(state.newBackend));

            auto* dc = contextData->feature.get();
            // Use given params if using DLSS passthrough
            const bool isPassthrough = state.newBackend == Upscaler::DLSSD || state.newBackend == Upscaler::DLSS;

            contextData->createParams = isPassthrough ? parameters : GetNGXParameters(API::DX12, false);
>>>>>>> upstream/master
            contextData->createParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, dc->GetFeatureFlags());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Width, dc->RenderWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Height, dc->RenderHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutWidth, dc->DisplayWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutHeight, dc->DisplayHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_PerfQualityValue, dc->PerfQualityValue());

            dc = nullptr;

            State::Instance().currentFeature = nullptr;

<<<<<<< HEAD
            if (state.gameQuirks & GameQuirk::FastFeatureReset)
            {
                LOG_DEBUG("sleeping before reset of current feature for 100ms (Fast Feature Reset)");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            else
            {
                LOG_DEBUG("sleeping before reset of current feature for 1000ms");
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
=======
            Util::DelayedDestroy(std::move(contextData->feature));
>>>>>>> upstream/master

            // LOG_DEBUG("sleeping before reset of current feature for 1000ms");
            // std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // contextData->feature.reset();
            // contextData->feature = nullptr;
        }
        else // Clean up state if no feature is set
        {
            LOG_ERROR("can't find handle {0} in Dx12Contexts!", handleId);

<<<<<<< HEAD
            state.newBackend = "";
=======
            state.newBackend = Upscaler::Reset;
>>>>>>> upstream/master
            state.changeBackend[handleId] = false;

            if (contextData->createParams != nullptr)
            {
                TryDestroyNGXParameters(contextData->createParams, NVNGXProxy::D3D12_DestroyParameters());
                contextData->createParams = nullptr;
            }

            contextData->changeBackendCounter = 0;

            return false;
        }

        return true;
    }

    // create new feature
    if (contextData->changeBackendCounter == 2)
    {
<<<<<<< HEAD
        LOG_INFO("Creating new {} upscaler", state.newBackend);
=======
        LOG_INFO("Creating new {} upscaler", UpscalerDisplayName(state.newBackend));
>>>>>>> upstream/master
        contextData->feature.reset();
        contextData->featureKey = state.newBackend;

<<<<<<< HEAD
        if (!GetFeature(state.newBackend, handleId, contextData->featureID, contextData->createParams, &contextData->feature))
=======
        if (!GetFeature(state.newBackend, handleId, contextData->createParams, &contextData->feature))
>>>>>>> upstream/master
        {
            LOG_ERROR("Upscaler can't created");
            return false;
        }

        return true;
    }

    // init feature
    if (contextData->changeBackendCounter == 3)
    {
        auto initResult = contextData->feature->Init(device, cmdList, contextData->createParams);

        contextData->changeBackendCounter = 0;

        if (!initResult)
        {
<<<<<<< HEAD
            LOG_ERROR("init failed with {0} feature", state.newBackend);

            if (state.newBackend != "dlssd")
            {
                if (cfg.Dx12Upscaler == "dlss")
                    state.newBackend = "xess";
                else
                    state.newBackend = "fsr21";
=======
            LOG_ERROR("init failed with {0} feature", UpscalerDisplayName(state.newBackend));

            if (state.newBackend != Upscaler::DLSSD)
            {
                if (cfg.Dx12Upscaler == Upscaler::DLSS)
                {
                    state.newBackend = Upscaler::XeSS;
                    ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "Falling back to XeSS" });
                }
                else
                {
                    state.newBackend = Upscaler::FSR21;
                    ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "Falling back to FSR 2.1.2" });
                }
>>>>>>> upstream/master
            }
            else
            {
                // Retry DLSSD
<<<<<<< HEAD
                state.newBackend = "dlssd";
            }

            state.changeBackend[handleId] = true;
            return NVSDK_NGX_Result_Success;
        }
        else
        {
            LOG_INFO("init successful for {0}, upscaler changed", state.newBackend);

            state.newBackend = "";
=======
                state.newBackend = Upscaler::DLSSD;
            }

            state.changeBackend[handleId] = true;

            return false;
        }
        else
        {
            LOG_INFO("init successful for {0}, upscaler changed", UpscalerDisplayName(state.newBackend));

            state.newBackend = Upscaler::Reset;
>>>>>>> upstream/master
            state.changeBackend[handleId] = false;
        }

        // If this is an OptiScaler fake NVNGX param table, delete it
        int optiParam = 0;

        if (contextData->createParams->Get("OptiScaler", &optiParam) == NVSDK_NGX_Result_Success && optiParam == 1)
        {
            TryDestroyNGXParameters(contextData->createParams, NVNGXProxy::D3D12_DestroyParameters());
            contextData->createParams = nullptr;
        }
    }

    // if initial feature can't be inited
    state.currentFeature = contextData->feature.get();

    if (state.currentFG != nullptr && state.activeFgInput == FGInput::Upscaler)
        state.currentFG->UpdateTarget();

    return true;
}
