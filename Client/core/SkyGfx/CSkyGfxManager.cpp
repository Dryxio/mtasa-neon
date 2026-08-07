#include "StdInc.h"
#include "CSkyGfxManager.h"
#include <game/RenderWare.h>
#include <game/RenderWareD3D.h>

namespace
{
    constexpr const char*    SETTINGS_SECTION = "skygfx";
    constexpr std::uintptr_t COLOR_FILTER_CALL = 0x704D1E;
    constexpr std::uintptr_t COLOR_FILTER_ORIGINAL = 0x703650;
    constexpr std::uintptr_t COLOR_FILTER_OVERRIDE_OPCODE = 0x7036EC;
    constexpr std::uintptr_t COLOR_FILTER_OVERRIDE_PRIMARY = 0x7036ED;
    constexpr std::uintptr_t COLOR_FILTER_OVERRIDE_SECONDARY = 0x70373E;
    constexpr std::uintptr_t RADIOSITY_CALL_DEBUG = 0x704D5D;
    constexpr std::uintptr_t RADIOSITY_CALL_NORMAL = 0x704FB3;
    constexpr std::uintptr_t RADIOSITY_ORIGINAL = 0x702080;
    constexpr std::uintptr_t POST_EFFECTS_FRONT_BUFFER = 0xC402D8;
    constexpr std::uintptr_t SCENE_CAMERA = 0xC1703C;
    constexpr BYTE           ORIGINAL_COLOR_FILTER_CALL[5] = {0xE8, 0x2D, 0xE9, 0xFF, 0xFF};
    constexpr BYTE           ORIGINAL_RADIOSITY_DEBUG_CALL[5] = {0xE8, 0x1E, 0xD3, 0xFF, 0xFF};
    constexpr BYTE           ORIGINAL_RADIOSITY_NORMAL_CALL[5] = {0xE8, 0xC8, 0xD0, 0xFF, 0xFF};

    bool ReadBooleanSetting(const char* key, bool defaultValue)
    {
        const SString value = GetApplicationSetting(SETTINGS_SECTION, key);
        return value.empty() ? defaultValue : GetApplicationSettingInt(SETTINGS_SECTION, key) != 0;
    }

    void __cdecl LogFromSkyGfx(const char* message)
    {
        WriteDebugEvent(SString("SkyGfx: %s", message ? message : "(no message)"));
    }

    template <class T>
    T ResolveExport(HMODULE module, const char* name)
    {
        static_assert(sizeof(T) == sizeof(FARPROC));
        const FARPROC address = GetProcAddress(module, name);
        T             result{};
        memcpy(&result, &address, sizeof(result));
        return result;
    }

    SkyGfxMTAColorV1 DecodeRgba(std::uint32_t rgba)
    {
        SkyGfxMTAColorV1 color{};
        color.red = static_cast<std::uint8_t>(rgba & 0xFFu);
        color.green = static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
        color.blue = static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
        color.alpha = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
        return color;
    }

    SkyGfxMTAColorV1 DecodeArgb(std::uint32_t argb)
    {
        SkyGfxMTAColorV1 color{};
        color.red = static_cast<std::uint8_t>((argb >> 16) & 0xFFu);
        color.green = static_cast<std::uint8_t>((argb >> 8) & 0xFFu);
        color.blue = static_cast<std::uint8_t>(argb & 0xFFu);
        color.alpha = static_cast<std::uint8_t>((argb >> 24) & 0xFFu);
        return color;
    }

    bool IsCallTo(std::uintptr_t callAddress, const void* target)
    {
        if (*reinterpret_cast<const BYTE*>(callAddress) != 0xE8)
            return false;

        std::int32_t relative = 0;
        memcpy(&relative, reinterpret_cast<const void*>(callAddress + 1), sizeof(relative));
        return callAddress + 5 + relative == reinterpret_cast<std::uintptr_t>(target);
    }

    bool WriteCodeBytes(std::uintptr_t address, const void* bytes, std::size_t size)
    {
        DWORD oldProtection = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtection))
            return false;

        memcpy(reinterpret_cast<void*>(address), bytes, size);
        const bool cacheFlushed = FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(address), size) != FALSE;

        DWORD      ignoredProtection = 0;
        const bool protectionRestored = VirtualProtect(reinterpret_cast<void*>(address), size, oldProtection, &ignoredProtection) != FALSE;
        return cacheFlushed && protectionRestored;
    }

    using ColorFilterFunction = void(__cdecl*)(std::uint32_t primaryRgba, std::uint32_t secondaryRgba);
    using RadiosityFunction = void(__cdecl*)(int intensityLimit, int filterPasses, int renderPasses, int intensity);

    void __cdecl DispatchColorFilter(std::uint32_t primaryRgba, std::uint32_t secondaryRgba)
    {
        if (SkyGfx::CManager::Get().RenderColorFilter(primaryRgba, secondaryRgba))
            return;

        reinterpret_cast<ColorFilterFunction>(COLOR_FILTER_ORIGINAL)(primaryRgba, secondaryRgba);
    }

    void __cdecl DispatchRadiosity(int intensityLimit, int filterPasses, int renderPasses, int intensity)
    {
        if (SkyGfx::CManager::Get().RenderRadiosity(intensityLimit, filterPasses, renderPasses, intensity))
            return;

        reinterpret_cast<RadiosityFunction>(RADIOSITY_ORIGINAL)(intensityLimit, filterPasses, renderPasses, intensity);
    }
}

namespace SkyGfx
{
    CManager& CManager::Get()
    {
        static CManager manager;
        return manager;
    }

    void CManager::LoadConfiguration()
    {
        m_config = {};
        m_config.enabled = ReadBooleanSetting("enabled", false) ? 1u : 0u;
        m_config.dualPass = ReadBooleanSetting("dual-pass", true) ? 1u : 0u;
        m_config.ps2ColorFilter = ReadBooleanSetting("ps2-color-filter", true) ? 1u : 0u;
        m_config.ps2ColorFilterBlur = ReadBooleanSetting("ps2-color-filter-blur", true) ? 1u : 0u;
        m_config.ps2ColorFilterPcTimecycle = ReadBooleanSetting("ps2-color-filter-pc-timecycle", true) ? 1u : 0u;
        m_config.ps2DepthBias = ReadBooleanSetting("ps2-depth-bias", true) ? 1u : 0u;
        m_config.ps2Radiosity = ReadBooleanSetting("ps2-radiosity", true) ? 1u : 0u;
        m_config.ps2RadiosityFilterPasses =
            static_cast<std::uint32_t>(std::clamp(GetApplicationSettingInt(SETTINGS_SECTION, "ps2-radiosity-filter-passes"), 1, 4));
        m_config.ps2RadiosityRenderPasses =
            static_cast<std::uint32_t>(std::clamp(GetApplicationSettingInt(SETTINGS_SECTION, "ps2-radiosity-render-passes"), 1, 4));
        m_config.ps2RadiosityIntensity = static_cast<std::uint32_t>(std::clamp(GetApplicationSettingInt(SETTINGS_SECTION, "ps2-radiosity-intensity"), 1, 255));

        if (GetApplicationSetting(SETTINGS_SECTION, "ps2-radiosity-filter-passes").empty())
            m_config.ps2RadiosityFilterPasses = 2;
        if (GetApplicationSetting(SETTINGS_SECTION, "ps2-radiosity-render-passes").empty())
            m_config.ps2RadiosityRenderPasses = 1;
        if (GetApplicationSetting(SETTINGS_SECTION, "ps2-radiosity-intensity").empty())
            m_config.ps2RadiosityIntensity = 35;

        const int preset = GetApplicationSettingInt(SETTINGS_SECTION, "preset");
        if (preset >= 0 && preset < static_cast<int>(SkyGfxMTAPreset::Count))
            m_config.preset = static_cast<SkyGfxMTAPreset>(preset);
        m_configurationLoaded = true;
    }

    bool CManager::ResolveApi()
    {
        m_getApiVersion = ResolveExport<SkyGfxMTAGetApiVersion>(m_module, "SkyGfxMTA_GetApiVersion");
        m_getCapabilities = ResolveExport<SkyGfxMTAGetCapabilities>(m_module, "SkyGfxMTA_GetCapabilities");
        m_initialize = ResolveExport<SkyGfxMTAInitialize>(m_module, "SkyGfxMTA_Initialize");
        m_applyConfig = ResolveExport<SkyGfxMTAApplyConfig>(m_module, "SkyGfxMTA_ApplyConfig");
        m_renderColorFilter = ResolveExport<SkyGfxMTARenderColorFilter>(m_module, "SkyGfxMTA_RenderColorFilter");
        m_renderRadiosity = ResolveExport<SkyGfxMTARenderRadiosity>(m_module, "SkyGfxMTA_RenderRadiosity");
        m_shutdown = ResolveExport<SkyGfxMTAShutdown>(m_module, "SkyGfxMTA_Shutdown");
        return m_getApiVersion && m_getCapabilities && m_initialize && m_applyConfig && m_renderColorFilter && m_renderRadiosity && m_shutdown;
    }

    bool CManager::Initialize()
    {
        if (m_status == IntegrationStatus::BridgeReady)
            return true;

        if (!m_configurationLoaded)
            LoadConfiguration();
        if (!m_config.enabled)
        {
            m_status = IntegrationStatus::Disabled;
            return true;
        }

        // Load only the dedicated bridge from MTA's own module directory. The
        // legacy ASI DllMain and its competing RenderWare hooks are never run.
        const SString modulePath = SharedUtil::CalcMTASAPath(SharedUtil::PathJoin("mta", "skygfx_mta.dll"));
        m_module = LoadLibraryW(SharedUtil::FromUTF8(modulePath).c_str());
        if (!m_module)
        {
            m_status = IntegrationStatus::ModuleMissing;
            WriteDebugEvent(SString("SkyGfx: optional bridge not found at %s", *modulePath));
            return false;
        }

        if (!ResolveApi() || m_getApiVersion() != SKY_GFX_MTA_API_VERSION)
        {
            m_status = IntegrationStatus::ApiMismatch;
            WriteDebugEvent("SkyGfx: bridge API is missing or incompatible");
            UnloadModule();
            return false;
        }

        SkyGfxMTAHostV1 host{};
        host.phase = SkyGfxMTAIntegrationPhase::PostMTAModules;
        host.log = LogFromSkyGfx;
        if (m_initialize(&host, &m_config) != SkyGfxMTAResult::Success)
        {
            m_status = IntegrationStatus::Failed;
            WriteDebugEvent("SkyGfx: bridge initialization failed");
            UnloadModule();
            return false;
        }

        m_capabilities = m_getCapabilities();
        m_status = IntegrationStatus::BridgeReady;
        UpdateColorFilterDispatch();
        UpdateRadiosityDispatch();
        WriteDebugEvent(SString("SkyGfx: controlled bridge ready (capabilities: 0x%08x)", m_capabilities));
        return true;
    }

    bool CManager::SetConfig(const SkyGfxMTAConfigV1& config, bool persist)
    {
        if (config.structSize < sizeof(SkyGfxMTAConfigV1) || config.enabled > 1 || config.dualPass > 1 || config.ps2ColorFilter > 1 ||
            config.ps2ColorFilterBlur > 1 || config.ps2ColorFilterPcTimecycle > 1 || config.ps2DepthBias > 1 || config.ps2Radiosity > 1 ||
            config.ps2RadiosityFilterPasses < 1 || config.ps2RadiosityFilterPasses > 4 || config.ps2RadiosityRenderPasses < 1 ||
            config.ps2RadiosityRenderPasses > 4 || config.ps2RadiosityIntensity > 255 || config.preset >= SkyGfxMTAPreset::Count)
        {
            return false;
        }

        m_config = config;
        m_configurationLoaded = true;
        if (persist)
        {
            SetApplicationSettingInt(SETTINGS_SECTION, "enabled", static_cast<int>(m_config.enabled));
            SetApplicationSettingInt(SETTINGS_SECTION, "preset", static_cast<int>(m_config.preset));
            SetApplicationSettingInt(SETTINGS_SECTION, "dual-pass", static_cast<int>(m_config.dualPass));
            SetApplicationSettingInt(SETTINGS_SECTION, "ps2-color-filter", static_cast<int>(m_config.ps2ColorFilter));
            SetApplicationSettingInt(SETTINGS_SECTION, "ps2-color-filter-blur", static_cast<int>(m_config.ps2ColorFilterBlur));
            SetApplicationSettingInt(SETTINGS_SECTION, "ps2-color-filter-pc-timecycle", static_cast<int>(m_config.ps2ColorFilterPcTimecycle));
            SetApplicationSettingInt(SETTINGS_SECTION, "ps2-depth-bias", static_cast<int>(m_config.ps2DepthBias));
            SetApplicationSettingInt(SETTINGS_SECTION, "ps2-radiosity", static_cast<int>(m_config.ps2Radiosity));
            SetApplicationSettingInt(SETTINGS_SECTION, "ps2-radiosity-filter-passes", static_cast<int>(m_config.ps2RadiosityFilterPasses));
            SetApplicationSettingInt(SETTINGS_SECTION, "ps2-radiosity-render-passes", static_cast<int>(m_config.ps2RadiosityRenderPasses));
            SetApplicationSettingInt(SETTINGS_SECTION, "ps2-radiosity-intensity", static_cast<int>(m_config.ps2RadiosityIntensity));
        }

        if (!m_config.enabled)
        {
            if (m_module && m_applyConfig)
                m_applyConfig(&m_config);
            Shutdown();
            return true;
        }

        if (!m_module)
            return Initialize();

        if (!m_applyConfig || m_applyConfig(&m_config) != SkyGfxMTAResult::Success)
            return false;

        UpdateColorFilterDispatch();
        UpdateRadiosityDispatch();
        return true;
    }

    bool CManager::IsColorFilterActive() const noexcept
    {
        return m_status == IntegrationStatus::BridgeReady && m_config.enabled != 0 && m_config.ps2ColorFilter != 0 &&
               m_config.preset == SkyGfxMTAPreset::PlayStation2 && (m_capabilities & SKY_GFX_MTA_CAPABILITY_PS2_COLOR_FILTER) != 0 &&
               m_colorFilterDispatchInstalled;
    }

    bool CManager::IsRadiosityActive() const noexcept
    {
        return m_status == IntegrationStatus::BridgeReady && m_config.enabled != 0 && m_config.ps2Radiosity != 0 &&
               m_config.preset == SkyGfxMTAPreset::PlayStation2 && (m_capabilities & SKY_GFX_MTA_CAPABILITY_PS2_RADIOSITY) != 0 && m_radiosityDispatchInstalled;
    }

    bool CManager::InstallColorFilterDispatch()
    {
        if (IsCallTo(COLOR_FILTER_CALL, reinterpret_cast<const void*>(&DispatchColorFilter)))
        {
            m_colorFilterDispatchInstalled = true;
            return true;
        }

        if (memcmp(reinterpret_cast<const void*>(COLOR_FILTER_CALL), ORIGINAL_COLOR_FILTER_CALL, sizeof(ORIGINAL_COLOR_FILTER_CALL)) != 0)
        {
            WriteDebugEvent("SkyGfx: GTA color-filter call is already modified; PS2 filter left disabled");
            return false;
        }

        const std::intptr_t relative = reinterpret_cast<std::uintptr_t>(&DispatchColorFilter) - (COLOR_FILTER_CALL + 5);
        if (relative < INT32_MIN || relative > INT32_MAX)
            return false;

        BYTE               patch[5] = {0xE8};
        const std::int32_t relative32 = static_cast<std::int32_t>(relative);
        memcpy(patch + 1, &relative32, sizeof(relative32));
        WriteCodeBytes(COLOR_FILTER_CALL, patch, sizeof(patch));

        m_colorFilterDispatchInstalled = IsCallTo(COLOR_FILTER_CALL, reinterpret_cast<const void*>(&DispatchColorFilter));
        if (m_colorFilterDispatchInstalled)
            WriteDebugEvent("SkyGfx: PS2 color-filter dispatch installed at GTA 0x704D1E");
        return m_colorFilterDispatchInstalled;
    }

    void CManager::RemoveColorFilterDispatch()
    {
        if (!m_colorFilterDispatchInstalled)
            return;

        if (IsCallTo(COLOR_FILTER_CALL, reinterpret_cast<const void*>(&DispatchColorFilter)))
        {
            WriteCodeBytes(COLOR_FILTER_CALL, ORIGINAL_COLOR_FILTER_CALL, sizeof(ORIGINAL_COLOR_FILTER_CALL));
            WriteDebugEvent("SkyGfx: restored GTA's original color-filter call");
        }
        else
        {
            WriteDebugEvent("SkyGfx: color-filter dispatch changed by another owner; original call not overwritten during shutdown");
        }
        m_colorFilterDispatchInstalled = false;
    }

    void CManager::UpdateColorFilterDispatch()
    {
        const bool requested = m_status == IntegrationStatus::BridgeReady && m_config.enabled != 0 && m_config.ps2ColorFilter != 0 &&
                               m_config.preset == SkyGfxMTAPreset::PlayStation2 && (m_capabilities & SKY_GFX_MTA_CAPABILITY_PS2_COLOR_FILTER) != 0;
        if (requested)
            InstallColorFilterDispatch();
        else
            RemoveColorFilterDispatch();
    }

    bool CManager::RenderColorFilter(std::uint32_t primaryRgba, std::uint32_t secondaryRgba)
    {
        if (!IsColorFilterActive() || !m_renderColorFilter)
            return false;

        IDirect3DDevice9* device = CGraphics::GetSingleton().GetDevice();
        RwRaster*         frontBuffer = *reinterpret_cast<RwRaster**>(POST_EFFECTS_FRONT_BUFFER);
        RwCamera*         camera = *reinterpret_cast<RwCamera**>(SCENE_CAMERA);
        if (!device || !frontBuffer || !camera || !camera->bufferColor)
            return false;

        auto* d3dRaster = reinterpret_cast<RwD3D9Raster*>(&frontBuffer->renderResource);
        if (!d3dRaster->texture)
            return false;

        // RenderWare creates this texture through MTA's proxy device, while
        // the bridge deliberately renders through the raw D3D device. Passing
        // the wrapper to the native runtime is invalid on D3D9On12, so unwrap
        // it at the integration boundary before SkyGfx issues its draw calls.
        IDirect3DBaseTexture9* sourceTexture = CDirect3DEvents9::GetRealTexture(d3dRaster->texture);
        if (!sourceTexture)
            return false;

        SkyGfxMTAColorFilterFrameV1 frame{};
        frame.d3dDevice = device;
        frame.sourceTexture = sourceTexture;
        frame.cameraWidth = static_cast<std::uint32_t>(camera->bufferColor->width);

        if (*reinterpret_cast<const BYTE*>(COLOR_FILTER_OVERRIDE_OPCODE) == 0xB8)
        {
            frame.primaryColor = DecodeArgb(*reinterpret_cast<const DWORD*>(COLOR_FILTER_OVERRIDE_PRIMARY));
            frame.secondaryColor = DecodeArgb(*reinterpret_cast<const DWORD*>(COLOR_FILTER_OVERRIDE_SECONDARY));
        }
        else
        {
            frame.primaryColor = DecodeRgba(primaryRgba);
            frame.secondaryColor = DecodeRgba(secondaryRgba);
        }

        const SkyGfxMTAResult result = m_renderColorFilter(&frame);
        if (result == SkyGfxMTAResult::Success)
            return true;

        if (!m_colorFilterFailureLogged)
        {
            WriteDebugEvent(SString("SkyGfx: PS2 color-filter render failed (%u); vanilla pass used for this frame", static_cast<unsigned int>(result)));
            m_colorFilterFailureLogged = true;
        }
        return false;
    }

    bool CManager::InstallRadiosityDispatch()
    {
        const void* dispatch = reinterpret_cast<const void*>(&DispatchRadiosity);
        if (IsCallTo(RADIOSITY_CALL_DEBUG, dispatch) && IsCallTo(RADIOSITY_CALL_NORMAL, dispatch))
        {
            m_radiosityDispatchInstalled = true;
            return true;
        }

        const bool debugOriginal =
            memcmp(reinterpret_cast<const void*>(RADIOSITY_CALL_DEBUG), ORIGINAL_RADIOSITY_DEBUG_CALL, sizeof(ORIGINAL_RADIOSITY_DEBUG_CALL)) == 0;
        const bool normalOriginal =
            memcmp(reinterpret_cast<const void*>(RADIOSITY_CALL_NORMAL), ORIGINAL_RADIOSITY_NORMAL_CALL, sizeof(ORIGINAL_RADIOSITY_NORMAL_CALL)) == 0;
        if (!debugOriginal || !normalOriginal)
        {
            WriteDebugEvent("SkyGfx: a GTA radiosity call is already modified; PS2 radiosity left disabled");
            return false;
        }

        auto installCall = [dispatch](std::uintptr_t address)
        {
            const std::intptr_t relative = reinterpret_cast<std::uintptr_t>(dispatch) - (address + 5);
            if (relative < INT32_MIN || relative > INT32_MAX)
                return false;

            BYTE               patch[5] = {0xE8};
            const std::int32_t relative32 = static_cast<std::int32_t>(relative);
            memcpy(patch + 1, &relative32, sizeof(relative32));
            return WriteCodeBytes(address, patch, sizeof(patch)) && IsCallTo(address, dispatch);
        };

        if (!installCall(RADIOSITY_CALL_DEBUG))
            return false;
        if (!installCall(RADIOSITY_CALL_NORMAL))
        {
            WriteCodeBytes(RADIOSITY_CALL_DEBUG, ORIGINAL_RADIOSITY_DEBUG_CALL, sizeof(ORIGINAL_RADIOSITY_DEBUG_CALL));
            return false;
        }

        m_radiosityDispatchInstalled = true;
        WriteDebugEvent("SkyGfx: PS2 radiosity dispatch installed at GTA 0x704D5D and 0x704FB3");
        return true;
    }

    void CManager::RemoveRadiosityDispatch()
    {
        if (!m_radiosityDispatchInstalled)
            return;

        const void* dispatch = reinterpret_cast<const void*>(&DispatchRadiosity);
        bool        restored = true;
        if (IsCallTo(RADIOSITY_CALL_DEBUG, dispatch))
            restored &= WriteCodeBytes(RADIOSITY_CALL_DEBUG, ORIGINAL_RADIOSITY_DEBUG_CALL, sizeof(ORIGINAL_RADIOSITY_DEBUG_CALL));
        else
            WriteDebugEvent("SkyGfx: debug radiosity dispatch changed by another owner; original call not overwritten during shutdown");

        if (IsCallTo(RADIOSITY_CALL_NORMAL, dispatch))
            restored &= WriteCodeBytes(RADIOSITY_CALL_NORMAL, ORIGINAL_RADIOSITY_NORMAL_CALL, sizeof(ORIGINAL_RADIOSITY_NORMAL_CALL));
        else
            WriteDebugEvent("SkyGfx: normal radiosity dispatch changed by another owner; original call not overwritten during shutdown");

        if (restored)
            WriteDebugEvent("SkyGfx: restored GTA's original radiosity calls");
        m_radiosityDispatchInstalled = false;
    }

    void CManager::UpdateRadiosityDispatch()
    {
        const bool requested = m_status == IntegrationStatus::BridgeReady && m_config.enabled != 0 && m_config.ps2Radiosity != 0 &&
                               m_config.preset == SkyGfxMTAPreset::PlayStation2 && (m_capabilities & SKY_GFX_MTA_CAPABILITY_PS2_RADIOSITY) != 0;
        if (requested)
            InstallRadiosityDispatch();
        else
            RemoveRadiosityDispatch();
    }

    bool CManager::RenderRadiosity(int intensityLimit, int filterPasses, int renderPasses, int intensity)
    {
        if (!IsRadiosityActive() || !m_renderRadiosity)
            return false;

        IDirect3DDevice9* device = CGraphics::GetSingleton().GetDevice();
        RwRaster*         frontBuffer = *reinterpret_cast<RwRaster**>(POST_EFFECTS_FRONT_BUFFER);
        RwCamera*         camera = *reinterpret_cast<RwCamera**>(SCENE_CAMERA);
        if (!device || !frontBuffer || !camera || !camera->bufferColor)
            return false;

        auto* d3dRaster = reinterpret_cast<RwD3D9Raster*>(&frontBuffer->renderResource);
        if (!d3dRaster->texture)
            return false;

        IDirect3DBaseTexture9* sourceTexture = CDirect3DEvents9::GetRealTexture(d3dRaster->texture);
        if (!sourceTexture)
            return false;

        SkyGfxMTARadiosityFrameV1 frame{};
        frame.d3dDevice = device;
        frame.sourceTexture = sourceTexture;
        frame.intensityLimit = static_cast<std::uint32_t>(std::clamp(intensityLimit, 0, 255));
        frame.cameraWidth = static_cast<std::uint32_t>(camera->bufferColor->width);
        frame.cameraHeight = static_cast<std::uint32_t>(camera->bufferColor->height);

        const SkyGfxMTAResult result = m_renderRadiosity(&frame);
        if (result == SkyGfxMTAResult::Success)
            return true;

        if (!m_radiosityFailureLogged)
        {
            WriteDebugEvent(SString("SkyGfx: PS2 radiosity render failed (%u; GTA args %d/%d/%d); vanilla pass used for this frame",
                                    static_cast<unsigned int>(result), filterPasses, renderPasses, intensity));
            m_radiosityFailureLogged = true;
        }
        return false;
    }

    void CManager::UnloadModule()
    {
        RemoveColorFilterDispatch();
        RemoveRadiosityDispatch();
        if (m_module)
            FreeLibrary(m_module);

        m_module = nullptr;
        m_capabilities = 0;
        m_getApiVersion = nullptr;
        m_getCapabilities = nullptr;
        m_initialize = nullptr;
        m_applyConfig = nullptr;
        m_renderColorFilter = nullptr;
        m_renderRadiosity = nullptr;
        m_shutdown = nullptr;
        m_colorFilterFailureLogged = false;
        m_radiosityFailureLogged = false;
    }

    void CManager::Shutdown()
    {
        RemoveColorFilterDispatch();
        RemoveRadiosityDispatch();
        if (m_module && m_shutdown)
            m_shutdown();
        UnloadModule();
        m_status = IntegrationStatus::Disabled;
    }
}
