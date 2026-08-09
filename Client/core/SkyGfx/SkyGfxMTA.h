#pragma once

#include <cstdint>

// Keep this ABI synchronized with Dryxio/skygfx src/mta/SkyGfxMTA.h. A C ABI
// prevents compiler-specific C++ types from coupling the optional DLL to core.
constexpr std::uint32_t SKY_GFX_MTA_API_VERSION = 5;

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
    SKY_GFX_MTA_CAPABILITY_YCBCR_CORRECTION = 1 << 5,
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
    std::uint32_t   enabled = 1;
    SkyGfxMTAPreset preset = SkyGfxMTAPreset::PlayStation2;
    std::uint32_t   dualPass = 1;
    std::uint32_t   ps2ColorFilter = 1;
    std::uint32_t   ps2ColorFilterBlur = 0;
    std::uint32_t   ps2ColorFilterPcTimecycle = 1;
    std::uint32_t   ps2DepthBias = 1;
    std::uint32_t   ps2Radiosity = 0;
    std::uint32_t   ps2RadiosityFilterPasses = 2;
    std::uint32_t   ps2RadiosityRenderPasses = 1;
    std::uint32_t   ps2RadiosityIntensity = 35;
    std::uint32_t   ycbcrCorrection = 1;
    float           lumaScale = 219.0f / 255.0f;
    float           lumaOffset = 16.0f / 255.0f;
    float           cbScale = 1.23f;
    float           cbOffset = 0.0f;
    float           crScale = 1.23f;
    float           crOffset = 0.0f;
};
static_assert(sizeof(SkyGfxMTAConfigV1) == 76, "SkyGfx MTA config ABI changed without an API version bump");

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

struct SkyGfxMTAYCbCrFrameV1
{
    std::uint32_t structSize = sizeof(SkyGfxMTAYCbCrFrameV1);
    void*         d3dDevice = nullptr;
    void*         sourceTexture = nullptr;
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
using SkyGfxMTARenderYCbCr = SkyGfxMTAResult(__cdecl*)(const SkyGfxMTAYCbCrFrameV1* frame);
using SkyGfxMTAInvalidateDeviceResources = SkyGfxMTAResult(__cdecl*)();
using SkyGfxMTARestoreDeviceResources = SkyGfxMTAResult(__cdecl*)();
using SkyGfxMTAShutdown = void(__cdecl*)();
