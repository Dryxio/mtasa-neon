#pragma once

#include "SkyGfxMTA.h"

namespace SkyGfx
{
    enum class IntegrationStatus
    {
        Disabled,
        ModuleMissing,
        ApiMismatch,
        BridgeReady,
        Failed,
    };

    class CManager final
    {
    public:
        enum RuntimeOverride : std::uint32_t
        {
            OverrideEnabled = 1u << 0,
            OverrideColorFilter = 1u << 1,
            OverrideColorFilterBlur = 1u << 2,
            OverridePcTimecycle = 1u << 3,
            OverrideDepthBias = 1u << 4,
            OverrideYCbCr = 1u << 5,
            OverrideRadiosity = 1u << 6,
            OverrideRadiosityIntensity = 1u << 7,
            OverrideRadiosityFilterPasses = 1u << 8,
            OverrideRadiosityRenderPasses = 1u << 9,
            OverrideAll = (1u << 10) - 1,
        };

        static CManager& Get();

        bool Initialize();
        void Shutdown();
        bool RenderColorFilter(std::uint32_t primaryRgba, std::uint32_t secondaryRgba);
        bool RenderRadiosity(int intensityLimit, int filterPasses, int renderPasses, int intensity);
        bool RenderYCbCrCorrection();
        void OnDeviceInvalidate();
        void OnDeviceRestore();

        const SkyGfxMTAConfigV1& GetConfig() const noexcept { return m_config; }
        const SkyGfxMTAConfigV1& GetUserConfig() const noexcept { return m_userConfig; }
        bool                     HasRuntimeOverrides() const noexcept { return m_runtimeOverrideMask != 0; }
        bool                     SetConfig(const SkyGfxMTAConfigV1& config, bool persist);
        bool                     SetRuntimeOverrides(const SkyGfxMTAConfigV1& config, std::uint32_t mask);
        bool                     ClearRuntimeOverrides();
        IntegrationStatus        GetStatus() const noexcept { return m_status; }
        std::uint32_t            GetCapabilities() const noexcept { return m_capabilities; }
        bool                     IsColorFilterActive() const noexcept;
        bool                     IsRadiosityActive() const noexcept;
        bool                     IsYCbCrCorrectionActive() const noexcept;

    private:
        CManager() = default;

        void              LoadConfiguration();
        void              UnloadModule();
        bool              ResolveApi();
        bool              InstallColorFilterDispatch();
        void              RemoveColorFilterDispatch();
        void              UpdateColorFilterDispatch();
        bool              InstallRadiosityDispatch();
        void              RemoveRadiosityDispatch();
        void              UpdateRadiosityDispatch();
        SkyGfxMTAConfigV1 BuildEffectiveConfig() const;

        SkyGfxMTAConfigV1                  m_config{};
        SkyGfxMTAConfigV1                  m_userConfig{};
        SkyGfxMTAConfigV1                  m_runtimeConfig{};
        std::uint32_t                      m_runtimeOverrideMask{};
        IntegrationStatus                  m_status{IntegrationStatus::Disabled};
        HMODULE                            m_module{};
        std::uint32_t                      m_capabilities{};
        bool                               m_configurationLoaded{};
        SkyGfxMTAGetApiVersion             m_getApiVersion{};
        SkyGfxMTAGetCapabilities           m_getCapabilities{};
        SkyGfxMTAInitialize                m_initialize{};
        SkyGfxMTAApplyConfig               m_applyConfig{};
        SkyGfxMTARenderColorFilter         m_renderColorFilter{};
        SkyGfxMTARenderRadiosity           m_renderRadiosity{};
        SkyGfxMTARenderYCbCr               m_renderYCbCr{};
        SkyGfxMTAInvalidateDeviceResources m_invalidateDeviceResources{};
        SkyGfxMTARestoreDeviceResources    m_restoreDeviceResources{};
        SkyGfxMTAShutdown                  m_shutdown{};
        bool                               m_colorFilterDispatchInstalled{};
        bool                               m_colorFilterFailureLogged{};
        bool                               m_radiosityDispatchInstalled{};
        bool                               m_radiosityFailureLogged{};
        bool                               m_ycbcrFailureLogged{};
    };
}
