#pragma once

#include <cstdint>

// Keep this ABI synchronized with Dryxio/skygfx src/mta/SkyGfxMTA.h. A C ABI
// prevents compiler-specific C++ types from coupling the optional DLL to core.
constexpr std::uint32_t SKY_GFX_MTA_API_VERSION = 3;

enum class SkyGfxMTAResult : std::uint32_t
{
    Success = 0,
    InvalidArgument,
    ApiVersionMismatch,
    NotInitialized,
    RenderFailed,
};

enum SkyGfxMTACapability : std::uint32_t
{
    SKY_GFX_MTA_CAPABILITY_NONE = 0,
    SKY_GFX_MTA_CAPABILITY_RENDER_PIPELINES = 1 << 0,
    SKY_GFX_MTA_CAPABILITY_POST_FX = 1 << 1,
    SKY_GFX_MTA_CAPABILITY_PS2_DEPTH_BIAS = 1 << 2,
    SKY_GFX_MTA_CAPABILITY_PS2_COLOR_FILTER = 1 << 3,
    SKY_GFX_MTA_CAPABILITY_PS2_RADIOSITY = 1 << 4,
};

enum class SkyGfxMTAIntegrationPhase : std::uint32_t
{
    Bootstrap = 0,
    PostMTAModules,
    DeviceReady,
};

enum class SkyGfxMTAPreset : std::uint32_t
{
    PlayStation2 = 0,
    PC,
    Mobile,
    Count,
};

struct SkyGfxMTAConfigV1
{
    std::uint32_t   structSize = sizeof(SkyGfxMTAConfigV1);
    std::uint32_t   enabled = 0;
    SkyGfxMTAPreset preset = SkyGfxMTAPreset::PlayStation2;
    std::uint32_t   dualPass = 1;
    std::uint32_t   ps2ColorFilter = 1;
    std::uint32_t   ps2ColorFilterBlur = 1;
    std::uint32_t   ps2ColorFilterPcTimecycle = 1;
    std::uint32_t   ps2DepthBias = 1;
    std::uint32_t   ps2Radiosity = 1;
    std::uint32_t   ps2RadiosityFilterPasses = 2;
    std::uint32_t   ps2RadiosityRenderPasses = 1;
    std::uint32_t   ps2RadiosityIntensity = 35;
};
static_assert(sizeof(SkyGfxMTAConfigV1) == 48, "SkyGfx MTA config ABI changed without an API version bump");

struct SkyGfxMTAColorV1
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;
};

struct SkyGfxMTAColorFilterFrameV1
{
    std::uint32_t    structSize = sizeof(SkyGfxMTAColorFilterFrameV1);
    void*            d3dDevice = nullptr;
    void*            sourceTexture = nullptr;
    SkyGfxMTAColorV1 primaryColor{};
    SkyGfxMTAColorV1 secondaryColor{};
    std::uint32_t    cameraWidth = 0;
};

struct SkyGfxMTARadiosityFrameV1
{
    std::uint32_t structSize = sizeof(SkyGfxMTARadiosityFrameV1);
    void*         d3dDevice = nullptr;
    void*         sourceTexture = nullptr;
    std::uint32_t intensityLimit = 0;
    std::uint32_t cameraWidth = 0;
    std::uint32_t cameraHeight = 0;
};

using SkyGfxMTALogCallback = void(__cdecl*)(const char* message);

struct SkyGfxMTAHostV1
{
    std::uint32_t             structSize = sizeof(SkyGfxMTAHostV1);
    std::uint32_t             apiVersion = SKY_GFX_MTA_API_VERSION;
    SkyGfxMTAIntegrationPhase phase = SkyGfxMTAIntegrationPhase::PostMTAModules;
    SkyGfxMTALogCallback      log = nullptr;
};

using SkyGfxMTAGetApiVersion = std::uint32_t(__cdecl*)();
using SkyGfxMTAGetCapabilities = std::uint32_t(__cdecl*)();
using SkyGfxMTAInitialize = SkyGfxMTAResult(__cdecl*)(const SkyGfxMTAHostV1* host, const SkyGfxMTAConfigV1* config);
using SkyGfxMTAApplyConfig = SkyGfxMTAResult(__cdecl*)(const SkyGfxMTAConfigV1* config);
using SkyGfxMTARenderColorFilter = SkyGfxMTAResult(__cdecl*)(const SkyGfxMTAColorFilterFrameV1* frame);
using SkyGfxMTARenderRadiosity = SkyGfxMTAResult(__cdecl*)(const SkyGfxMTARadiosityFrameV1* frame);
using SkyGfxMTAShutdown = void(__cdecl*)();
