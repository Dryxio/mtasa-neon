/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x / Neon
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClient2DFXManager.h
 *  PURPOSE:     Resource-owned model 2DFX state and native integration
 *
 *****************************************************************************/
#pragma once

#include <CVector.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CClientManager;
class CResource;
struct C2DFXNativeEffect;

enum class e2dEffectType : std::uint8_t
{
    LIGHT = 0,
    PARTICLE = 1,
    UNKNOWN = 2,
    ATTRACTOR = 3,
    SUN_GLARE = 4,
    FURNITURE = 5,
    ENEX = 6,
    ROADSIGN = 7,
    TRIGGER_POINT = 8,
    COVER_POINT = 9,
    ESCALATOR = 10,
    NONE = 11,
};

enum class e2dCoronaFlashType : std::uint8_t
{
    DEFAULT = 0,
    RANDOM,
    RANDOM_WHEN_WET,
    ANIM_SPEED_4X,
    ANIM_SPEED_2X,
    ANIM_SPEED_1X,
    WARNLIGHT,
    TRAFFICLIGHT,
    TRAINCROSSING,
    BRIDGE,
    ONLY_RAIN,
    ON5_OFF5,
    ON6_OFF4,
    ON4_OFF6,
};

enum class e2dEffectProperty : std::uint8_t
{
    POSITION,
    FAR_CLIP_DISTANCE,
    LIGHT_RANGE,
    CORONA_SIZE,
    SHADOW_SIZE,
    SHADOW_MULT,
    FLASH_TYPE,
    CORONA_REFLECTION,
    FLARE_TYPE,
    SHADOW_DISTANCE,
    OFFSET,
    COLOR,
    CORONA_NAME,
    SHADOW_NAME,
    FLAGS,
    PARTICLE_NAME,
    SIZE,
    ROTATION,
    TEXT_1,
    TEXT_2,
    TEXT_3,
    TEXT_4,
    BOTTOM,
    TOP,
    END,
    DIRECTION,
};

struct S2DFXData
{
    CVector position{};
    float drawDistance{};
    float lightRange{};
    float coronaSize{};
    float shadowSize{};
    std::uint8_t shadowMultiplier{};
    std::int8_t shadowDistance{};
    e2dCoronaFlashType flashType{e2dCoronaFlashType::DEFAULT};
    bool coronaReflection{};
    std::uint8_t flareType{};
    CVector offset{};
    std::uint32_t color{0xFFFFFFFFu};
    std::uint16_t flags{};
    std::string coronaName;
    std::string shadowName;
    std::string particleName;
    CVector size{};
    CVector rotation{};
    std::string text[4];
    CVector bottom{};
    CVector top{};
    CVector end{};
    std::uint8_t direction{};
};

class CClient2DFXManager
{
public:
    struct SImpl;

    static CClient2DFXManager& GetSingleton();
    void Initialize(CClientManager* manager);

    bool IsValidModel(std::uint32_t model) const;
    bool IsModelLoaded(std::uint32_t model) const;
    std::uint32_t GetCount(std::uint32_t model, bool includeCustom = true) const;
    e2dEffectType GetType(std::uint32_t model, std::uint32_t index) const;
    bool GetData(std::uint32_t model, std::uint32_t index, S2DFXData& out) const;

    bool Add(CResource* owner, std::uint32_t model, e2dEffectType type, const S2DFXData& data);
    bool Remove(CResource* owner, std::uint32_t model, std::uint32_t index);
    bool Restore(CResource* owner, std::uint32_t model, std::uint32_t index);
    bool ResetModel(CResource* owner, std::uint32_t model);
    bool SetProperty(CResource* owner, std::uint32_t model, std::uint32_t index, e2dEffectProperty property, const S2DFXData& value);
    bool ResetProperty(CResource* owner, std::uint32_t model, std::uint32_t index, e2dEffectProperty property);
    void ReleaseResource(CResource* owner);

    static bool IsPropertyValid(e2dEffectType type, e2dEffectProperty property);
    static void CopyProperty(S2DFXData& destination, const S2DFXData& source, e2dEffectProperty property);

    C2DFXNativeEffect* ResolveNativeEffect(void* modelInfo, void* geometry, std::uint32_t pluginCount, std::uint32_t index);
    void OnEntityEffectsCreated(void* entity);

private:
    CClient2DFXManager() = default;
    ~CClient2DFXManager();
    CClient2DFXManager(const CClient2DFXManager&) = delete;
    CClient2DFXManager& operator=(const CClient2DFXManager&) = delete;

    std::unique_ptr<SImpl> m_impl;
};
