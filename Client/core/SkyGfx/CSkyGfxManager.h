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
        static CManager& Get();

        bool Initialize();
        void Shutdown();
        bool RenderColorFilter(std::uint32_t primaryRgba, std::uint32_t secondaryRgba);
        bool RenderRadiosity(int intensityLimit, int filterPasses, int renderPasses, int intensity);

        const SkyGfxMTAConfigV1& GetConfig() const noexcept { return m_config; }
        bool                     SetConfig(const SkyGfxMTAConfigV1& config, bool persist);
        IntegrationStatus        GetStatus() const noexcept { return m_status; }
        std::uint32_t            GetCapabilities() const noexcept { return m_capabilities; }
        bool                     IsColorFilterActive() const noexcept;
        bool                     IsRadiosityActive() const noexcept;

    private:
        CManager() = default;

        void LoadConfiguration();
        void UnloadModule();
        bool ResolveApi();
        bool InstallColorFilterDispatch();
        void RemoveColorFilterDispatch();
        void UpdateColorFilterDispatch();
        bool InstallRadiosityDispatch();
        void RemoveRadiosityDispatch();
        void UpdateRadiosityDispatch();

        SkyGfxMTAConfigV1          m_config{};
        IntegrationStatus          m_status{IntegrationStatus::Disabled};
        HMODULE                    m_module{};
        std::uint32_t              m_capabilities{};
        bool                       m_configurationLoaded{};
        SkyGfxMTAGetApiVersion     m_getApiVersion{};
        SkyGfxMTAGetCapabilities   m_getCapabilities{};
        SkyGfxMTAInitialize        m_initialize{};
        SkyGfxMTAApplyConfig       m_applyConfig{};
        SkyGfxMTARenderColorFilter m_renderColorFilter{};
        SkyGfxMTARenderRadiosity   m_renderRadiosity{};
        SkyGfxMTAShutdown          m_shutdown{};
        bool                       m_colorFilterDispatchInstalled{};
        bool                       m_colorFilterFailureLogged{};
        bool                       m_radiosityDispatchInstalled{};
        bool                       m_radiosityFailureLogged{};
    };
}
