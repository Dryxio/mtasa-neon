/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CCoronasSA.cpp
 *  PURPOSE:     Corona entity manager
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CCoronasSA.h"
#include "CDistantLightsSA.h"
#include "HookSystem.h"
#include "CDistantLightNativeTransitionsSA.h"
#include "CColModelSA.h"
#include <game/CPointLights.h>
#include "CRegisteredCoronaSA.h"
#include "CBuildingSA.h"
#include "CDummyPoolSA.h"
#include "CEntitySA.h"
#include "CFileLoaderSA.h"
#include "CGameSA.h"
#include "CModelInfoSA.h"
#include "CPoolsSA.h"
#include "CPoolSAInterface.h"
#include <core/CCoreInterface.h>
#include <game/CCamera.h>
#include <game/CWorld.h>
#include <game/RenderWare.h>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

extern CGameSA*        pGame;
extern CCoreInterface* g_pCore;

using SharedUtil::CalcMTASAPath;

namespace
{
    // GTA stores the corona array in the executable's data segment. Keep the
    // replacement alive for the rest of the process because GTA can render
    // coronas before and after MTA recreates its CGameSA wrapper objects.
    void InstallDistantLightNativeTransitions()
    {
        static bool attempted = false;
        if (attempted)
            return;
        attempted = true;
        // Refuse unknown code instead of overwriting another hook or a different
        // executable layout. Each adapter replays the overwritten instructions.
        const BYTE first[] = {0x8B, 0x46, 0x14, 0x33, 0xC9};
        const BYTE normal[] = {0x8B, 0x4E, 0x14, 0x52, 0x50};
        const BYTE update[] = {0x8B, 0x4E, 0x14, 0x8B, 0x44, 0x24, 0x1C};
        const BYTE traffic[] = {0x68, 0x00, 0x00, 0x48, 0x42};
        if (memcmp(reinterpret_cast<void*>(0x6FCEA9), first, sizeof(first)) || memcmp(reinterpret_cast<void*>(0x6FCFC4), normal, sizeof(normal)) ||
            memcmp(reinterpret_cast<void*>(0x6FD040), update, sizeof(update)) || memcmp(reinterpret_cast<void*>(0x49DCF3), traffic, sizeof(traffic)))
        {
            OutputReleaseLine("[Project2DFX] Native light transitions skipped: unexpected executable instructions");
            return;
        }
        HookInstall(0x6FCEA9, DistantLightNativeTransitions::First, 5);
        HookInstall(0x6FCFC4, DistantLightNativeTransitions::Normal, 5);
        HookInstall(0x6FD040, DistantLightNativeTransitions::Update, 7);
        HookInstall(0x49DCF3, DistantLightNativeTransitions::Traffic, 5);
    }

    CRegisteredCoronaSAInterface* g_pCoronaArray = reinterpret_cast<CRegisteredCoronaSAInterface*>(ARRAY_CORONAS);

    void PatchCoronaArrayPointer(std::uintptr_t address, const void* value)
    {
        MemPut<DWORD>(address, reinterpret_cast<DWORD>(value));
    }

    BYTE* CoronaField(CRegisteredCoronaSAInterface* corona, std::size_t offset)
    {
        return reinterpret_cast<BYTE*>(corona) + offset;
    }

    // Project2DFX source reference: ThirteenAG/III.VC.SA.IV.Project2DFX,
    // source/LODLights.ixx and SALodLights/dllmain.cpp (MIT license).
    // MTA consumes Project2DFX's light data through its own loader and a
    // private buffered sprite queue rather than importing the ASI or consuming
    // GTA/MTA's shared corona pool.
    constexpr DWORD MAX_DISTANT_LIGHT_CORONAS = 25000;
    constexpr DWORD EFFECT_LIGHT = 0;
    constexpr WORD  LIGHT_FLAG_WITHOUT_CORONA = 1 << 3;
    constexpr WORD  LIGHT_FLAG_AT_NIGHT = 1 << 6;
    constexpr DWORD IPL_INSTANCE_DONT_STREAM = 1 << 9;

    constexpr DWORD FUNC_CSPRITE_FLUSH_BUFFER = 0x70CF20;
    constexpr DWORD FUNC_CSPRITE_RENDER_BUFFERED_XLU = 0x70E780;
    constexpr DWORD VAR_SCENE = 0xC17038;
    constexpr DWORD VAR_RW_ENGINE_INSTANCE = 0xC97B24;
    constexpr DWORD VAR_WEATHER_FOGGINESS = 0xC81300;

    constexpr DWORD RW_RENDER_STATE_TEXTURE_RASTER = 1;
    constexpr DWORD RW_RENDER_STATE_Z_TEST_ENABLE = 6;
    constexpr DWORD RW_RENDER_STATE_Z_WRITE_ENABLE = 8;
    constexpr DWORD RW_RENDER_STATE_SOURCE_BLEND = 10;
    constexpr DWORD RW_RENDER_STATE_DESTINATION_BLEND = 11;
    constexpr DWORD RW_RENDER_STATE_VERTEX_ALPHA_ENABLE = 12;
    constexpr DWORD RW_BLEND_ONE = 2;

    struct SGtaScene
    {
        RpWorld*  world;
        RwCamera* camera;
    };

    using RwRenderStateFunction = int(__cdecl*)(DWORD, void*);
    using FlushSpriteBufferFunction = void(__cdecl*)();
    using RenderBufferedSpriteFunction = void(__cdecl*)(float, float, float, float, float, BYTE, BYTE, BYTE, short, float, float, BYTE);

    void* RwStateValue(DWORD value)
    {
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
    }

    struct S2dEffectLightData
    {
        BYTE       red;
        BYTE       green;
        BYTE       blue;
        BYTE       alpha;
        float      coronaFarClip;
        float      pointLightRange;
        float      coronaSize;
        float      shadowSize;
        WORD       flags;
        BYTE       flashType;
        bool       enableReflection;
        BYTE       flareType;
        BYTE       shadowColorMultiplier;
        char       shadowZDistance;
        char       offsetX;
        char       offsetY;
        char       offsetZ;
        char       pad[2];
        RwTexture* coronaTexture;
        RwTexture* shadowTexture;
        int        field38;
        int        field3C;
    };

    struct S2dEffect
    {
        CVector            position;
        DWORD              type;
        S2dEffectLightData light;
    };

    static_assert(sizeof(S2dEffectLightData) == 0x30, "Invalid GTA 2DFX light layout");
    static_assert(sizeof(S2dEffect) == 0x40, "Invalid GTA 2DFX entry layout");

    struct SDistantLight
    {
        CVector    position;
        RwTexture* texture;
        float      coronaSize;
        float      objectDrawDistance;
        BYTE       red;
        BYTE       green;
        BYTE       blue;
        BYTE       alpha;
        BYTE       flashType;
        BYTE       flareType;
        BYTE       noDistance;
        bool       trafficLight;
        bool       trafficLightFacesEastWest;
        float      searchlightHeight;
    };

    using SDistantLightDefinition = DistantLights::Definition<CVector>;

    struct SDistantLightKey
    {
        int   x;
        int   y;
        int   z;
        DWORD color;
        BYTE  type = 0, flashType = 0;
        bool  searchlight = false, facesEastWest = false;
        float size = 0.0f, drawDistance = 0.0f;

        bool operator==(const SDistantLightKey&) const = default;
    };

    struct SDistantLightKeyHash
    {
        std::size_t operator()(const SDistantLightKey& key) const noexcept
        {
            std::size_t hash = std::hash<int>{}(key.x);
            hash ^= std::hash<int>{}(key.y) + 0x9E3779B9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<int>{}(key.z) + 0x9E3779B9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<DWORD>{}(key.color) + 0x9E3779B9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    struct SDistantLightCandidate
    {
        float       distanceSquared;
        std::size_t index;
    };

    struct SDistantLightRenderInstance
    {
        CVector    position;
        RwTexture* texture;
        float      size;
        float      range;
        BYTE       red;
        BYTE       green;
        BYTE       blue;
        BYTE       alpha;
    };

    struct SDistantLightConeInstance
    {
        CVector             position;
        DistantLights::Cone cone;
        SColor              color;
    };

    struct SDistantLightSourceInstance
    {
        CVector   position;
        CVector4D rotation;
        int       modelId;
    };

    static_assert(sizeof(SDistantLightSourceInstance) == 0x20, "Unexpected deferred Project2DFX source size");

    std::vector<SDistantLightConeInstance> g_DistantLightCones;
    std::vector<SDistantLightCandidate>    g_DistantLightConeCandidates;

    bool                  g_bDistantLightsEnabled = false;
    bool                  g_bDistantLightSearchlightsEnabled = true;
    bool                  g_bDistantLightsNeedRebuild = true;
    float                 g_fDistantLightsDrawDistance = 2000.0f;
    SDistantLightSettings g_DistantLightSettings;

    float GetDistantLightRange()
    {
        return DistantLights::ResolveRange(g_DistantLightSettings.automaticDistance, *reinterpret_cast<const float*>(0xB7C4F0), g_fDistantLightsDrawDistance);
    }
    float                                                      g_fDistantLightsCoronaRadiusMultiplier = 0.25f;
    std::vector<SDistantLight>                                 g_DistantLights;
    std::vector<SDistantLightCandidate>                        g_DistantLightCandidates;
    std::vector<SDistantLightRenderInstance>                   g_DistantLightRenderQueue;
    DistantLights::Sources<SDistantLightSourceInstance>        g_DistantLightSourceInstances;
    std::unordered_set<SDistantLightKey, SDistantLightKeyHash> g_DistantLightKeys;
    DWORD                                                      g_dwDistantLightEntitiesScanned = 0;
    DWORD                                                      g_dwDistantLightEffectsScanned = 0;
    DWORD                                                      g_dwDistantLightEffectsFound = 0;

    using SDistantLightDefinitions = std::unordered_map<WORD, std::vector<SDistantLightDefinition>>;

    SDistantLightDefinitions             g_DistantLightDefinitions;
    std::vector<SDistantLightDefinition> g_AdditionalDistantLightDefinitions;
    bool                                 g_bDistantLightDefinitionsLoaded = false;
    bool                                 g_bDistantLightDefinitionsLoadAttempted = false;
    bool                                 g_bAdditionalDistantLightsRegistered = false;
    bool                                 g_bDistantLightStorageAllocated = false;

    void EnsureDistantLightStorageAllocated()
    {
        if (g_bDistantLightStorageAllocated)
            return;

        g_DistantLightKeys.reserve(24000);
        g_DistantLightCandidates.reserve(MAX_DISTANT_LIGHT_CORONAS);
        g_DistantLightRenderQueue.reserve(MAX_DISTANT_LIGHT_CORONAS);
        g_bDistantLightStorageAllocated = true;
    }

    CBaseModelInfoSAInterface* GetModelInfoByName(const char* name, int* index)
    {
        return reinterpret_cast<CBaseModelInfoSAInterface*(__cdecl*)(const char*, int*)>(0x4C5940)(name, index);
    }

    S2dEffect* GetModel2dEffect(CBaseModelInfoSAInterface* modelInfo, int index)
    {
        return reinterpret_cast<S2dEffect*(__thiscall*)(CBaseModelInfoSAInterface*, int)>(0x4C4C70)(modelInfo, index);
    }

    bool LoadDistantLightDefinitions(SDistantLightDefinitions& modelDefinitions, std::vector<SDistantLightDefinition>& additionalDefinitions)
    {
        const SString path = CalcMTASAPath("MTA\\data\\SALodLights.dat");
        FILE*         file = File::Fopen(path, "r");
        if (!file)
        {
            OutputReleaseLine(SString("[Project2DFX] Could not open %s; falling back to GTA's embedded 2DFX effects", path.c_str()));
            return false;
        }

        int   currentModel = -1;
        bool  additionalCoronas = false;
        DWORD namedModels = 0;
        DWORD unresolvedModels = 0;
        DWORD searchlights = 0;
        char  line[512];
        while (fgets(line, sizeof(line), file))
        {
            char* begin = line;
            while (*begin == ' ' || *begin == '\t')
                ++begin;
            if (!*begin || *begin == '\r' || *begin == '\n' || *begin == '#')
                continue;

            if (*begin == '%')
            {
                char* end = begin + strlen(begin);
                while (end > begin && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t'))
                    *--end = '\0';

                additionalCoronas = strcmp(begin + 1, "additional_coronas") == 0;
                currentModel = -1;
                if (!additionalCoronas)
                {
                    int model = -1;
                    if (GetModelInfoByName(begin + 1, &model) && model >= 0 && static_cast<DWORD>(model) < pGame->GetBaseIDforTXD())
                    {
                        currentModel = model;
                        ++namedModels;
                    }
                    else
                        ++unresolvedModels;
                }
                continue;
            }

            SDistantLightDefinition definition{};
            if ((!additionalCoronas && currentModel < 0) || !DistantLights::Parse(begin, definition))
                continue;

            if (definition.drawSearchlight)
                ++searchlights;

            if (additionalCoronas)
                additionalDefinitions.push_back(definition);
            else
                modelDefinitions[static_cast<WORD>(currentModel)].push_back(definition);
        }
        fclose(file);

        std::size_t definitionCount = additionalDefinitions.size();
        for (const auto& [model, definitions] : modelDefinitions)
            definitionCount += definitions.size();
        OutputReleaseLine(SString("[Project2DFX] loaded %u DAT definitions for %u models (%u unresolved, %u searchlight definitions)",
                                  static_cast<DWORD>(definitionCount), namedModels, unresolvedModels, searchlights));
        return !modelDefinitions.empty() || !additionalDefinitions.empty();
    }

    void ClearActiveDistantLights()
    {
        g_DistantLightCones.clear();
        g_DistantLightRenderQueue.clear();
    }

    using DistantLights::GetNightAlpha;
    using DistantLights::IsDistantLightOn;

    bool IsTrafficLightOn(const SDistantLight& light, BYTE minute)
    {
        const bool isYellow = light.red >= 250 && light.green >= 100 && light.blue <= 150;
        const bool isRed = light.red >= 250 && light.green < 100 && light.blue == 0;
        const bool isGreen = light.red == 0 && light.green >= 250 && light.blue == 0;

        bool isYellowTime = minute % 10 == 9;
        bool isRedTime = minute % 20 < 9;
        bool isGreenTime = !isYellowTime && !isRedTime;
        if (light.trafficLightFacesEastWest)
            std::swap(isRedTime, isGreenTime);

        return (isYellow && isYellowTime) || (isRed && isRedTime) || (isGreen && isGreenTime);
    }

    bool AddDistantLight(const CVector& position, const SDistantLightDefinition& definition, float objectDrawDistance, bool trafficLightFacesEastWest,
                         std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen, float searchlightHeight = 0.0f)
    {
        if (!std::isfinite(position.fX) || !std::isfinite(position.fY) || !std::isfinite(position.fZ) || std::abs(position.fX) > 1.0e7f ||
            std::abs(position.fY) > 1.0e7f || position.fZ < -15.0f || position.fZ > 1030.0f)
            return false;

        const DWORD color = static_cast<DWORD>(definition.red) | static_cast<DWORD>(definition.green) << 8 | static_cast<DWORD>(definition.blue) << 16 |
                            static_cast<DWORD>(definition.alpha) << 24;
        const SDistantLightKey key{
            static_cast<int>(std::lround(position.fX * 10.0f)),
            static_cast<int>(std::lround(position.fY * 10.0f)),
            static_cast<int>(std::lround(position.fZ * 10.0f)),
            color,
            definition.noDistance,
            definition.flashType,
            definition.drawSearchlight,
            trafficLightFacesEastWest,
            definition.coronaSize,
            objectDrawDistance,
        };
        if (!seen.insert(key).second)
            return false;

        g_DistantLights.push_back({
            position,
            nullptr,
            definition.coronaSize,
            objectDrawDistance,
            definition.red,
            definition.green,
            definition.blue,
            definition.alpha,
            definition.flashType,
            0,
            definition.noDistance,
            definition.trafficLight,
            trafficLightFacesEastWest,
            definition.drawSearchlight ? searchlightHeight : 0.0f,
        });
        return true;
    }

    bool EnsureDistantLightDefinitionsLoaded()
    {
        if (!g_bDistantLightDefinitionsLoadAttempted)
        {
            g_bDistantLightDefinitionsLoadAttempted = true;
            g_bDistantLightDefinitionsLoaded = LoadDistantLightDefinitions(g_DistantLightDefinitions, g_AdditionalDistantLightDefinitions);
        }

        if (g_bDistantLightDefinitionsLoaded && !g_bAdditionalDistantLightsRegistered)
        {
            g_bAdditionalDistantLightsRegistered = true;
            for (const SDistantLightDefinition& definition : g_AdditionalDistantLightDefinitions)
                AddDistantLight(definition.localPosition, definition, definition.drawDistance, false, g_DistantLightKeys);
        }
        return g_bDistantLightDefinitionsLoaded;
    }

    float GetSearchlightHeight(const CBaseModelInfoSAInterface* model)
    {
        if (!model->pColModel)
            return 0.0f;
        const auto& bounds = model->pColModel->m_bounds;
        return std::max(0.0f, bounds.m_vecMax.fZ - bounds.m_vecMin.fZ);
    }

    void AddDatDistantLightsForEntity(CEntitySAInterface* entity, const SDistantLightDefinitions& modelDefinitions,
                                      std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen)
    {
        ++g_dwDistantLightEntitiesScanned;
        if (!entity || entity->m_areaCode != 0)
            return;

        const auto definitions = modelDefinitions.find(entity->m_nModelIndex);
        if (definitions == modelDefinitions.end())
            return;

        auto* modelInfo = reinterpret_cast<CBaseModelInfoSAInterface**>(CModelInfoSAInterface::ms_modelInfoPtrs)[entity->m_nModelIndex];
        if (!modelInfo)
            return;

        for (const SDistantLightDefinition& definition : definitions->second)
        {
            CVector worldPosition;
            entity->TransformFromObjectSpace(worldPosition, definition.localPosition);
            const float configuredDrawDistance = definition.drawDistance > 0.0f ? definition.drawDistance : modelInfo->fLodDistanceUnscaled;
            const float heading = entity->m_transform.m_heading;
            const float worldOffsetX = definition.localPosition.fX * std::cos(heading) - definition.localPosition.fY * std::sin(heading);
            const float worldOffsetY = definition.localPosition.fX * std::sin(heading) + definition.localPosition.fY * std::cos(heading);
            AddDistantLight(worldPosition, definition, std::min(configuredDrawDistance, modelInfo->fLodDistanceUnscaled),
                            std::abs(worldOffsetX) > std::abs(worldOffsetY), seen, GetSearchlightHeight(modelInfo));
        }
    }

    CVector4D GetDistantLightSourceRotation(const SFileObjectInstance& instance)
    {
        const CVector4D& rotation = instance.rotation;
        const bool       useFullQuaternion = std::abs(rotation.fX) > 0.05f || std::abs(rotation.fY) > 0.05f ||
                                       ((static_cast<DWORD>(instance.interiorID) & IPL_INSTANCE_DONT_STREAM) && rotation.fX != 0.0f && rotation.fY != 0.0f);

        // CFileLoader::LoadObjectInstance conjugates tilted quaternions before
        // CMatrix::SetRotate. Its common Z-only path instead derives a heading
        // with the opposite sign. Store one canonical final quaternion so the
        // deferred transform matches the entity path in both cases.
        if (useFullQuaternion)
            return {-rotation.fX, -rotation.fY, -rotation.fZ, rotation.fW};

        const float multiplier = rotation.fZ < 0.0f ? 2.0f : -2.0f;
        const float halfHeading = std::acos(rotation.fW) * multiplier * 0.5f;
        return {0.0f, 0.0f, std::sin(halfHeading), std::cos(halfHeading)};
    }

    void AddDatDistantLightsForSourceInstance(const SDistantLightSourceInstance& instance, const SDistantLightDefinitions& modelDefinitions,
                                              std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen)
    {
        ++g_dwDistantLightEntitiesScanned;
        if (instance.modelId < 0 || static_cast<DWORD>(instance.modelId) >= pGame->GetBaseIDforTXD())
            return;

        const auto definitions = modelDefinitions.find(static_cast<WORD>(instance.modelId));
        if (definitions == modelDefinitions.end())
            return;

        auto* modelInfo = reinterpret_cast<CBaseModelInfoSAInterface**>(CModelInfoSAInterface::ms_modelInfoPtrs)[instance.modelId];
        if (!modelInfo)
            return;

        for (const SDistantLightDefinition& definition : definitions->second)
        {
            const CVector worldPosition = DistantLights::Transform(instance, definition.localPosition);
            const CVector worldOffset = worldPosition - instance.position;
            const float   configuredDrawDistance = definition.drawDistance > 0.0f ? definition.drawDistance : modelInfo->fLodDistanceUnscaled;
            AddDistantLight(worldPosition, definition, std::min(configuredDrawDistance, modelInfo->fLodDistanceUnscaled),
                            std::abs(worldOffset.fX) > std::abs(worldOffset.fY), seen, GetSearchlightHeight(modelInfo));
        }
    }

    template <class TTransform>
    void AddNativeDistantLightsForInstance(int modelId, TTransform transformPosition, std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen)
    {
        ++g_dwDistantLightEntitiesScanned;
        if (modelId < 0 || static_cast<DWORD>(modelId) >= pGame->GetBaseIDforTXD())
            return;

        auto* modelInfo = reinterpret_cast<CBaseModelInfoSAInterface**>(CModelInfoSAInterface::ms_modelInfoPtrs)[modelId];
        if (!modelInfo || !modelInfo->ucNumOf2DEffects)
            return;

        g_dwDistantLightEffectsScanned += modelInfo->ucNumOf2DEffects;

        for (int effectIndex = 0; effectIndex < modelInfo->ucNumOf2DEffects; ++effectIndex)
        {
            S2dEffect* effect = GetModel2dEffect(modelInfo, effectIndex);
            if (!effect || effect->type != EFFECT_LIGHT)
                continue;

            ++g_dwDistantLightEffectsFound;
            if (!effect->light.coronaTexture || effect->light.coronaSize <= 0.0f || effect->light.flags & LIGHT_FLAG_WITHOUT_CORONA)
                continue;

            // Phase 1 renders static night lights only. Traffic lights and train
            // crossings require their live controller state and are deliberately
            // left to GTA until that state is integrated.
            if (!(effect->light.flags & LIGHT_FLAG_AT_NIGHT) || effect->light.flashType == 7 || effect->light.flashType == 8 || effect->light.flashType == 10)
                continue;

            const CVector worldPosition = transformPosition(effect->position);
            if (worldPosition.fZ < -15.0f || worldPosition.fZ > 1030.0f)
                continue;

            const DWORD color = static_cast<DWORD>(effect->light.red) | static_cast<DWORD>(effect->light.green) << 8 |
                                static_cast<DWORD>(effect->light.blue) << 16 | static_cast<DWORD>(effect->light.alpha) << 24;
            const SDistantLightKey key{
                static_cast<int>(std::lround(worldPosition.fX * 10.0f)),
                static_cast<int>(std::lround(worldPosition.fY * 10.0f)),
                static_cast<int>(std::lround(worldPosition.fZ * 10.0f)),
                color,
            };
            if (!seen.insert(key).second)
                continue;

            g_DistantLights.push_back({
                worldPosition,
                effect->light.coronaTexture,
                effect->light.coronaSize,
                std::max(modelInfo->fLodDistanceUnscaled, effect->light.coronaFarClip),
                effect->light.red,
                effect->light.green,
                effect->light.blue,
                effect->light.alpha,
                effect->light.flashType,
                effect->light.flareType,
                false,
                false,
                false,
            });
        }
    }

    void AddNativeDistantLightsForEntity(CEntitySAInterface* entity, std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen)
    {
        if (!entity || entity->m_areaCode != 0)
            return;

        AddNativeDistantLightsForInstance(
            entity->m_nModelIndex,
            [entity](const CVector& localPosition)
            {
                CVector worldPosition;
                entity->TransformFromObjectSpace(worldPosition, localPosition);
                return worldPosition;
            },
            seen);
    }

    void AddNativeDistantLightsForSourceInstance(const SDistantLightSourceInstance& instance, std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen)
    {
        AddNativeDistantLightsForInstance(
            instance.modelId, [&instance](const CVector& localPosition) { return DistantLights::Transform(instance, localPosition); }, seen);
    }

    void ConsumeDeferredDistantLightSources(bool loadedDat)
    {
        g_DistantLightSourceInstances.Replay(
            [loadedDat](const SDistantLightSourceInstance& instance)
            {
                if (loadedDat)
                    AddDatDistantLightsForSourceInstance(instance, g_DistantLightDefinitions, g_DistantLightKeys);
                else
                    AddNativeDistantLightsForSourceInstance(instance, g_DistantLightKeys);
            });
    }

    template <class T>
    bool AddNativeDistantLightsFromPool(DWORD poolAddress, std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen)
    {
        auto** poolPointer = reinterpret_cast<CPoolSAInterface<T>**>(poolAddress);
        if (!poolPointer || !*poolPointer)
            return false;

        CPoolSAInterface<T>* pool = *poolPointer;
        for (int i = 0; i < pool->m_nSize; ++i)
        {
            if (pool->IsContains(i))
                AddNativeDistantLightsForEntity(pool->GetObject(i), seen);
        }
        return true;
    }

    template <class T>
    bool AddDatDistantLightsFromPool(DWORD poolAddress, const SDistantLightDefinitions& modelDefinitions,
                                     std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen)
    {
        auto** poolPointer = reinterpret_cast<CPoolSAInterface<T>**>(poolAddress);
        if (!poolPointer || !*poolPointer)
            return false;

        CPoolSAInterface<T>* pool = *poolPointer;
        for (int i = 0; i < pool->m_nSize; ++i)
        {
            if (pool->IsContains(i))
                AddDatDistantLightsForEntity(pool->GetObject(i), modelDefinitions, seen);
        }
        return true;
    }

}  // namespace

CRegisteredCoronaSAInterface* CCoronasSA::GetCoronaArray()
{
    return g_pCoronaArray;
}

void CCoronasSA::RelocateCoronaArray()
{
    static bool bPatched = false;
    if (bPatched)
        return;

    static CRegisteredCoronaSAInterface coronaArray[MAX_CORONAS]{};
    g_pCoronaArray = coronaArray;

    // Every instruction below directly addresses CCoronas::aCoronas in the
    // SA 1.0 US executable. Relocating all field references lets GTA keep its
    // original corona implementation while iterating over MTA's larger array.
    PatchCoronaArrayPointer(0x6FAACF, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FAEA0, coronaArray);
    PatchCoronaArrayPointer(0x6FAEB7, coronaArray + MAX_CORONAS);
    PatchCoronaArrayPointer(0x6FAF42, &coronaArray[0].pEntityAttachedTo);
    PatchCoronaArrayPointer(0x6FB648, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FB657, CoronaField(&coronaArray[MAX_CORONAS], 0x36));
    PatchCoronaArrayPointer(0x6FB6CF, &coronaArray[0].FadedIntensity);
    PatchCoronaArrayPointer(0x6FB9B8, &coronaArray[MAX_CORONAS].FadedIntensity);

    PatchCoronaArrayPointer(0x6FC2E8, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC318, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC341, &coronaArray[0].FadedIntensity);
    PatchCoronaArrayPointer(0x6FC34A, &coronaArray[0].FadedIntensity);
    PatchCoronaArrayPointer(0x6FC351, CoronaField(&coronaArray[0], 0x34));
    PatchCoronaArrayPointer(0x6FC358, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC365, &coronaArray[0].JustCreated);
    PatchCoronaArrayPointer(0x6FC36B, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC37A, &coronaArray[0].Red);
    PatchCoronaArrayPointer(0x6FC384, &coronaArray[0].Green);
    PatchCoronaArrayPointer(0x6FC38E, &coronaArray[0].Blue);
    PatchCoronaArrayPointer(0x6FC398, &coronaArray[0].Intensity);
    PatchCoronaArrayPointer(0x6FC3A1, &coronaArray[0].Coordinates);
    PatchCoronaArrayPointer(0x6FC3B9, &coronaArray[0].Size);
    PatchCoronaArrayPointer(0x6FC3C3, &coronaArray[0].NormalAngle);
    PatchCoronaArrayPointer(0x6FC3CD, &coronaArray[0].Range);
    PatchCoronaArrayPointer(0x6FC3D7, &coronaArray[0].pTex);
    PatchCoronaArrayPointer(0x6FC3E1, &coronaArray[0].FlareType);
    PatchCoronaArrayPointer(0x6FC3EB, &coronaArray[0].ReflectionType);
    PatchCoronaArrayPointer(0x6FC3F1, CoronaField(&coronaArray[0], 0x34));
    PatchCoronaArrayPointer(0x6FC3FB, &coronaArray[0].RegisteredThisFrame);
    PatchCoronaArrayPointer(0x6FC403, CoronaField(&coronaArray[0], 0x34));
    PatchCoronaArrayPointer(0x6FC40D, &coronaArray[0].PullTowardsCam);
    PatchCoronaArrayPointer(0x6FC417, &coronaArray[0].FadeSpeed);
    PatchCoronaArrayPointer(0x6FC432, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC44A, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC454, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC45A, &coronaArray[0].pEntityAttachedTo);
    PatchCoronaArrayPointer(0x6FC478, &coronaArray[0].FadedIntensity);
    PatchCoronaArrayPointer(0x6FC496, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC4AC, CoronaField(&coronaArray[0], 0x36));
    PatchCoronaArrayPointer(0x6FC4B2, &coronaArray[0].pEntityAttachedTo);
    PatchCoronaArrayPointer(0x6FC538, &coronaArray[0].Identifier);
    PatchCoronaArrayPointer(0x6FC555, &coronaArray[0].Coordinates);
    PatchCoronaArrayPointer(0x6FC56D, &coronaArray[0].NormalAngle);

    // GTA's native RegisterCorona and UpdateCoronaCoors searches deliberately
    // remain limited to the first 64 slots. Those slots service vanilla
    // effects, while MTA allocates scripted coronas from the entire relocated
    // array through CCoronasSA::FindFreeCorona.
    MemPut<DWORD>(0x6FAAD4, MAX_CORONAS);
    MemPut<DWORD>(0x6FAF4A, MAX_CORONAS);

    bPatched = true;
}

CCoronasSA::CCoronasSA()
{
    RelocateCoronaArray();
    InstallDistantLightNativeTransitions();

    for (int i = 0; i < MAX_CORONAS; i++)
    {
        Coronas[i] = new CRegisteredCoronaSA(&GetCoronaArray()[i], i);
    }
}

CCoronasSA::~CCoronasSA()
{
    // The active map stores wrappers owned by this manager. Release every slot
    // before deleting them so reconnecting cannot retain dangling wrappers.
    ClearActiveDistantLights();
    g_DistantLights.clear();
    g_DistantLightCandidates.clear();
    g_DistantLightRenderQueue.clear();
    g_DistantLightSourceInstances.clear();
    g_DistantLightKeys.clear();
    g_DistantLightDefinitions.clear();
    g_AdditionalDistantLightDefinitions.clear();
    g_bDistantLightDefinitionsLoaded = false;
    g_bDistantLightDefinitionsLoadAttempted = false;
    g_bAdditionalDistantLightsRegistered = false;
    g_bDistantLightStorageAllocated = false;
    g_dwDistantLightEntitiesScanned = 0;
    g_dwDistantLightEffectsScanned = 0;
    g_dwDistantLightEffectsFound = 0;
    g_bDistantLightsEnabled = false;
    DistantLightNativeTransitions::enabled = false;
    g_bDistantLightSearchlightsEnabled = true;
    g_DistantLightSettings = {};
    g_bDistantLightsNeedRebuild = true;

    for (int i = 0; i < MAX_CORONAS; i++)
    {
        delete Coronas[i];
    }
}

CRegisteredCorona* CCoronasSA::GetCorona(DWORD ID)
{
    return (CRegisteredCorona*)Coronas[ID];
}

CRegisteredCorona* CCoronasSA::CreateCorona(DWORD Identifier, CVector* position)
{
    CRegisteredCoronaSA* corona;
    corona = (CRegisteredCoronaSA*)FindCorona(Identifier);

    if (!corona)
        corona = (CRegisteredCoronaSA*)FindFreeCorona();

    if (corona)
    {
        RwTexture* texture = GetTexture(CoronaType::CORONATYPE_SHINYSTAR);
        if (texture)
        {
            corona->Init(Identifier);
            corona->SetPosition(position);
            corona->SetTexture(texture);
            return (CRegisteredCorona*)corona;
        }
    }

    return (CRegisteredCorona*)NULL;
}

CRegisteredCorona* CCoronasSA::FindFreeCorona()
{
    for (int i = 2; i < MAX_CORONAS; i++)
    {
        if (Coronas[i]->GetIdentifier() == 0)
        {
            return Coronas[i];
        }
    }
    return (CRegisteredCorona*)NULL;
}

CRegisteredCorona* CCoronasSA::FindCorona(DWORD Identifier)
{
    for (int i = 0; i < MAX_CORONAS; i++)
    {
        if (Coronas[i]->GetIdentifier() == Identifier)
        {
            return Coronas[i];
        }
    }
    return (CRegisteredCorona*)NULL;
}

RwTexture* CCoronasSA::GetTexture(CoronaType type)
{
    if ((DWORD)type < MAX_CORONA_TEXTURES)
        return (RwTexture*)(*(DWORD*)(ARRAY_CORONA_TEXTURES + static_cast<DWORD>(type) * sizeof(DWORD)));
    else
        return NULL;
}

void CCoronasSA::DisableSunAndMoon(bool bDisabled)
{
    static BYTE byteOriginal = 0;
    if (bDisabled && !byteOriginal)
    {
        byteOriginal = *(BYTE*)FUNC_DoSunAndMoon;
        MemPut<BYTE>(FUNC_DoSunAndMoon, 0xC3);
    }
    else if (!bDisabled && byteOriginal)
    {
        MemPut<BYTE>(FUNC_DoSunAndMoon, byteOriginal);
        byteOriginal = 0;
    }
}

/*
    Enable or disable corona rain reflections.
    ucEnabled:
     0 - disabled
     1 - enabled
     2 - force enabled (render even if there is no rain)
*/
void CCoronasSA::SetCoronaReflectionsEnabled(unsigned char ucEnabled)
{
    m_ucCoronaReflectionsEnabled = ucEnabled;

    if (ucEnabled == 0)
    {
        // Disable corona rain reflections
        // Return out CCoronas::RenderReflections()
        MemPut<BYTE>(0x6FB630, 0xC3);
    }
    else
    {
        // Enable corona rain reflections
        // Re-enable CCoronas::RenderReflections()
        MemPut<BYTE>(0x6FB630, 0xD9);
    }

    if (ucEnabled == 2)
    {
        // Force enable corona reflections (render even if there is no rain)
        // Disable fWetGripScale check
        MemPut<BYTE>(0x6FB645, 0xEB);

        // Patch "fld fWetGripScale" to "fld fOne"
        MemCpy((void*)0x6FB906, "\x24\x86\x85\x00", 4);
    }
    else
    {
        // Restore patched code
        MemPut<BYTE>(0x6FB645, 0x7A);
        MemCpy((void*)0x6FB906, "\x08\x13\xC8\x00", 4);
    }
}

unsigned char CCoronasSA::GetCoronaReflectionsEnabled()
{
    return m_ucCoronaReflectionsEnabled;
}

void CCoronasSA::SetDistantLightsEnabled(bool enabled)
{
    if (g_bDistantLightsEnabled == enabled)
        return;

    g_bDistantLightsEnabled = enabled;
    DistantLightNativeTransitions::enabled = enabled;
    if (enabled)
        g_bDistantLightsNeedRebuild = true;
    else
        ClearActiveDistantLights();
}

void CCoronasSA::SetDistantLightSearchlightsEnabled(bool enabled)
{
    // Keep the preference independent of the master switch and discard queued
    // cones immediately, even if settings are applied between update/render.
    g_bDistantLightSearchlightsEnabled = enabled;
    if (!enabled)
        g_DistantLightCones.clear();
}

bool CCoronasSA::SetDistantLightSettings(const SDistantLightSettings& settings)
{
    if (!settings.IsValid())
        return false;
    g_DistantLightSettings = settings;
    ClearActiveDistantLights();
    return true;
}

void CCoronasSA::SetDistantLightsAutomaticDrawDistance(bool enabled)
{
    g_DistantLightSettings.automaticDistance = enabled;
    ClearActiveDistantLights();
}

bool CCoronasSA::GetDistantLightsEnabled() const
{
    return g_bDistantLightsEnabled;
}

bool CCoronasSA::SetDistantLightsDrawDistance(float distance)
{
    if (!std::isfinite(distance) || distance < 300.0f || distance > 5000.0f)
        return false;

    g_fDistantLightsDrawDistance = distance;
    // Existing Lua callers explicitly requesting a distance retain manual behavior.
    g_DistantLightSettings.automaticDistance = false;
    return true;
}

bool CCoronasSA::SetDistantLightsCoronaRadiusMultiplier(float multiplier)
{
    if (!std::isfinite(multiplier) || multiplier < 0.1f || multiplier > 1.0f)
        return false;

    // Project2DFX exposes this multiplier because DAT entries intentionally
    // vary widely in size. Keeping it configurable avoids flattening those
    // authored differences while allowing Neon to use a less bloated default.
    g_fDistantLightsCoronaRadiusMultiplier = multiplier;
    return true;
}

void CCoronasSA::RebuildDistantLights()
{
    if (!g_bDistantLightsEnabled)
        return;

    EnsureDistantLightStorageAllocated();
    ClearActiveDistantLights();
    // Derived lights and DAT definitions must be replaced, otherwise edits and
    // removed definitions survive rebuilds. IPL placements outlive streaming.
    g_DistantLights.clear();
    g_DistantLightKeys.clear();
    g_DistantLightDefinitions.clear();
    g_AdditionalDistantLightDefinitions.clear();
    g_bDistantLightDefinitionsLoadAttempted = false;
    g_bAdditionalDistantLightsRegistered = false;
    g_dwDistantLightEntitiesScanned = g_dwDistantLightEffectsScanned = g_dwDistantLightEffectsFound = 0;
    const bool loadedDat = EnsureDistantLightDefinitionsLoaded();
    if (!g_DistantLightSourceInstances.empty())
        ConsumeDeferredDistantLightSources(loadedDat);

    bool buildingPoolReady = false;
    bool dummyPoolReady = false;
    if (loadedDat)
    {
        buildingPoolReady = AddDatDistantLightsFromPool<CBuildingSAInterface>(CLASS_CBuildingPool, g_DistantLightDefinitions, g_DistantLightKeys);
        dummyPoolReady = AddDatDistantLightsFromPool<CEntitySAInterface>(CLASS_CDummyPool, g_DistantLightDefinitions, g_DistantLightKeys);
    }
    else
    {
        buildingPoolReady = AddNativeDistantLightsFromPool<CBuildingSAInterface>(CLASS_CBuildingPool, g_DistantLightKeys);
        dummyPoolReady = AddNativeDistantLightsFromPool<CEntitySAInterface>(CLASS_CDummyPool, g_DistantLightKeys);
    }
    g_bDistantLightsNeedRebuild = !(buildingPoolReady && dummyPoolReady);

    const SString message("[Project2DFX] source=%s scanned entities=%u effects=%u lights=%u; accepted definitions=%u", loadedDat ? "DAT" : "GTA",
                          g_dwDistantLightEntitiesScanned, g_dwDistantLightEffectsScanned, g_dwDistantLightEffectsFound, g_DistantLights.size());
    OutputReleaseLine(message);
    if (g_pCore)
        g_pCore->ChatEcho(message, false);
}

void CCoronasSA::CaptureDistantLight(const SFileObjectInstance& instance, CEntitySAInterface* entity)
{
    if (!entity)
        return;

    // Remember every observed static IPL placement, including those streamed
    // while enabled, so later DAT reloads do not lose off-screen lights.
    if (entity->m_areaCode == 0 && instance.modelID >= 0 && static_cast<DWORD>(instance.modelID) < pGame->GetBaseIDforTXD())
        g_DistantLightSourceInstances.Remember({instance.position, GetDistantLightSourceRotation(instance), instance.modelID});
    if (!g_bDistantLightsEnabled)
        return;

    EnsureDistantLightStorageAllocated();

    if (EnsureDistantLightDefinitionsLoaded())
        AddDatDistantLightsForEntity(entity, g_DistantLightDefinitions, g_DistantLightKeys);
    else
        AddNativeDistantLightsForEntity(entity, g_DistantLightKeys);
}

void CCoronasSA::DoPulseDistantLights()
{
    if (!g_bDistantLightsEnabled)
        return;

    if (g_bDistantLightsNeedRebuild)
        RebuildDistantLights();

    const BYTE hour = *reinterpret_cast<BYTE*>(0xB70153);
    const BYTE minute = *reinterpret_cast<BYTE*>(0xB70152);
    if ((hour < 20 && hour >= 7) || pGame->GetWorld()->GetCurrentArea() != 0)
    {
        ClearActiveDistantLights();
        return;
    }

    CMatrix cameraMatrix;
    pGame->GetCamera()->GetMatrix(&cameraMatrix);
    const CVector& cameraPosition = cameraMatrix.vPos;
    const float    farDistance = GetDistantLightRange();
    const float    farDistanceSquared = farDistance * farDistance;

    std::vector<SDistantLightCandidate>& candidates = g_DistantLightCandidates;
    candidates.clear();
    g_DistantLightConeCandidates.clear();
    candidates.reserve(std::min<std::size_t>(g_DistantLights.size(), MAX_DISTANT_LIGHT_CORONAS));
    for (std::size_t i = 0; i < g_DistantLights.size(); ++i)
    {
        const SDistantLight& light = g_DistantLights[i];
        if (light.trafficLight && !IsTrafficLightOn(light, minute))
            continue;

        const float dx = cameraPosition.fX - light.position.fX;
        const float dy = cameraPosition.fY - light.position.fY;
        const float dz = cameraPosition.fZ - light.position.fZ;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (g_bDistantLightSearchlightsEnabled && light.searchlightHeight > 0 && distanceSquared > 45.0f * 45.0f && distanceSquared < 300.0f * 300.0f)
            g_DistantLightConeCandidates.push_back({distanceSquared, i});
        const float nearDistance = light.noDistance ? 0.0f : std::max(0.0f, light.objectDrawDistance - 30.0f);
        if ((light.noDistance || distanceSquared > nearDistance * nearDistance) && distanceSquared < farDistanceSquared)
            candidates.push_back({distanceSquared, i});
    }

    DistantLights::KeepNearest(candidates, MAX_DISTANT_LIGHT_CORONAS);

    g_DistantLightRenderQueue.clear();
    const BYTE  nightAlpha = GetNightAlpha(hour, minute);
    const DWORD timeMs = *reinterpret_cast<DWORD*>(0xB7CB7C);
    // Cones have an independent near/far window; corona LOD fading must not
    // suppress a beam on a nearby building. Bound native geometry work.
    DistantLights::KeepNearest(g_DistantLightConeCandidates, 32);
    g_DistantLightCones.clear();
    for (const auto& candidate : g_DistantLightConeCandidates)
    {
        const auto&         light = g_DistantLights[candidate.index];
        DistantLights::Cone cone;
        if (DistantLights::MakeCone(std::sqrt(candidate.distanceSquared), light.searchlightHeight, light.coronaSize,
                                    IsDistantLightOn(light.flashType, candidate.index, timeMs), cone))
        {
            const float intensity = cone.intensity * (light.alpha / 255.0f);
            g_DistantLightCones.push_back({light.position, cone,
                                           SColorRGBA(static_cast<BYTE>(light.red * intensity), static_cast<BYTE>(light.green * intensity),
                                                      static_cast<BYTE>(light.blue * intensity), 255)});
        }
    }
    for (const SDistantLightCandidate& candidate : candidates)
    {
        const std::size_t    index = candidate.index;
        const SDistantLight& light = g_DistantLights[index];
        const float          distance = std::sqrt(candidate.distanceSquared);
        const auto           corona =
            DistantLights::Evaluate(distance, light.objectDrawDistance, farDistance, light.coronaSize, g_fDistantLightsCoronaRadiusMultiplier, light.noDistance,
                                    light.alpha, nightAlpha, IsDistantLightOn(light.flashType, index, timeMs), g_DistantLightSettings);
        DistantLights::PointLight point;
        if (DistantLights::MakePointLight(light.noDistance, distance, light.coronaSize, light.objectDrawDistance, corona.intensity, light.red, light.green,
                                          light.blue, point))
        {
            // Native colors are floats: going through SColor would clamp the
            // authored intensity boost before GTA receives it.
            DistantLights::SubmitPointLight(reinterpret_cast<DistantLights::AddPointLight>(0x7000E0), light.position, point);
        }

        g_DistantLightRenderQueue.push_back({
            light.position,
            light.texture,
            corona.radius,
            farDistance,
            light.red,
            light.green,
            light.blue,
            corona.alpha,
        });
    }
}

void CCoronasSA::RenderDistantLights()
{
    if (!g_bDistantLightsEnabled || (g_DistantLightRenderQueue.empty() && g_DistantLightCones.empty()))
        return;

    const auto* scene = reinterpret_cast<const SGtaScene*>(VAR_SCENE);
    RwCamera*   camera = scene ? scene->camera : nullptr;
    if (!camera || !camera->bufferColor || camera->bufferColor->width <= 0 || camera->bufferColor->height <= 0)
        return;

    const DWORD rwEngine = *reinterpret_cast<const DWORD*>(VAR_RW_ENGINE_INSTANCE);
    if (!rwEngine)
        return;

    const auto renderStateSet = reinterpret_cast<RwRenderStateFunction>(*reinterpret_cast<const DWORD*>(rwEngine + 0x20));
    const auto renderStateGet = reinterpret_cast<RwRenderStateFunction>(*reinterpret_cast<const DWORD*>(rwEngine + 0x24));
    const auto flushSpriteBuffer = reinterpret_cast<FlushSpriteBufferFunction>(FUNC_CSPRITE_FLUSH_BUFFER);
    const auto renderBufferedSprite = reinterpret_cast<RenderBufferedSpriteFunction>(FUNC_CSPRITE_RENDER_BUFFERED_XLU);
    if (!renderStateSet || !renderStateGet || !flushSpriteBuffer || !renderBufferedSprite)
        return;

    if (!g_DistantLightCones.empty())
    {
        // GTA's heli cone renderer already has Neon's color hooks. Its post
        // function restores defaults, so save the caller's actual states too.
        DistantLights::WithConeStates(renderStateGet, renderStateSet,
                                      [&]()
                                      {
                                          flushSpriteBuffer();
                                          auto* pointLights = pGame->GetPointLights();
                                          pointLights->PreRenderHeliLights();
                                          for (const auto& light : g_DistantLightCones)
                                          {
                                              CVector end = light.position;
                                              end.fZ -= light.cone.length;
                                              pointLights->RenderHeliLight(light.position, end, light.cone.startRadius, light.cone.endRadius, false,
                                                                           light.color);
                                          }
                                          pointLights->PostRenderHeliLights();
                                      });
    }

    void* oldTextureRaster = nullptr;
    void* oldZTest = nullptr;
    void* oldZWrite = nullptr;
    void* oldSourceBlend = nullptr;
    void* oldDestinationBlend = nullptr;
    void* oldVertexAlpha = nullptr;
    if (!renderStateGet(RW_RENDER_STATE_TEXTURE_RASTER, &oldTextureRaster) || !renderStateGet(RW_RENDER_STATE_Z_TEST_ENABLE, &oldZTest) ||
        !renderStateGet(RW_RENDER_STATE_Z_WRITE_ENABLE, &oldZWrite) || !renderStateGet(RW_RENDER_STATE_SOURCE_BLEND, &oldSourceBlend) ||
        !renderStateGet(RW_RENDER_STATE_DESTINATION_BLEND, &oldDestinationBlend) || !renderStateGet(RW_RENDER_STATE_VERTEX_ALPHA_ENABLE, &oldVertexAlpha))
        return;

    // The sprite buffer is global to GTA. Flush anything left by the native
    // pass before changing its render states, then restore every state so MTA
    // renderers that follow this hook inherit exactly what GTA left behind.
    flushSpriteBuffer();

    renderStateSet(RW_RENDER_STATE_Z_WRITE_ENABLE, RwStateValue(FALSE));
    renderStateSet(RW_RENDER_STATE_VERTEX_ALPHA_ENABLE, RwStateValue(TRUE));
    renderStateSet(RW_RENDER_STATE_SOURCE_BLEND, RwStateValue(RW_BLEND_ONE));
    renderStateSet(RW_RENDER_STATE_DESTINATION_BLEND, RwStateValue(RW_BLEND_ONE));
    renderStateSet(RW_RENDER_STATE_Z_TEST_ENABLE, RwStateValue(TRUE));

    const float     screenWidth = static_cast<float>(camera->bufferColor->width);
    const float     screenHeight = static_cast<float>(camera->bufferColor->height);
    const float     fogginess = *reinterpret_cast<const float*>(VAR_WEATHER_FOGGINESS);
    const RwMatrix& viewMatrix = camera->matrix;
    // Project2DFX registers DAT lights as HEADLIGHT coronas. GTA's stock slots
    // currently share coronastar, but retaining that semantic lets texture
    // replacements distinguish distant lights in the future.
    RwTexture* defaultTexture = GetTexture(CoronaType::CORONATYPE_HEADLIGHT);
    RwRaster*  lastRaster = nullptr;

    for (const SDistantLightRenderInstance& light : g_DistantLightRenderQueue)
    {
        const float viewX =
            light.position.fX * viewMatrix.right.x + light.position.fY * viewMatrix.up.x + light.position.fZ * viewMatrix.at.x + viewMatrix.pos.x;
        const float viewY =
            light.position.fX * viewMatrix.right.y + light.position.fY * viewMatrix.up.y + light.position.fZ * viewMatrix.at.y + viewMatrix.pos.y;
        const float viewZ =
            light.position.fX * viewMatrix.right.z + light.position.fY * viewMatrix.up.z + light.position.fZ * viewMatrix.at.z + viewMatrix.pos.z;
        if (viewZ <= 1.0f || viewZ > light.range)
            continue;

        const float inverseViewZ = 1.0f / viewZ;
        const float screenX = viewX * screenWidth * inverseViewZ;
        const float screenY = viewY * screenHeight * inverseViewZ;
        const float renderHeight = light.size * screenHeight * inverseViewZ;
        if (renderHeight < 0.35f)
            continue;

        const float halfRange = light.range * 0.5f;
        const float rangeFade = viewZ > halfRange ? 1.0f - (viewZ - halfRange) / halfRange : 1.0f;
        const short intensity = static_cast<short>(light.alpha * std::clamp(rangeFade, 0.0f, 1.0f));
        if (intensity <= 0)
            continue;

        RwTexture* texture = light.texture ? light.texture : defaultTexture;
        RwRaster*  raster = texture ? texture->raster : nullptr;
        if (!raster)
            continue;
        if (lastRaster != raster)
        {
            flushSpriteBuffer();
            lastRaster = raster;
            renderStateSet(RW_RENDER_STATE_TEXTURE_RASTER, raster);
        }

        const float colorFogMultiplier = std::min(40.0f, viewZ) * fogginess * 0.025f + 1.0f;
        renderBufferedSprite(screenX, screenY, viewZ, renderHeight, renderHeight * colorFogMultiplier,
                             static_cast<BYTE>(static_cast<float>(light.red) / colorFogMultiplier),
                             static_cast<BYTE>(static_cast<float>(light.green) / colorFogMultiplier),
                             static_cast<BYTE>(static_cast<float>(light.blue) / colorFogMultiplier), intensity, inverseViewZ * 20.0f, 0.0f, 0xFF);
    }

    flushSpriteBuffer();
    renderStateSet(RW_RENDER_STATE_TEXTURE_RASTER, oldTextureRaster);
    renderStateSet(RW_RENDER_STATE_Z_TEST_ENABLE, oldZTest);
    renderStateSet(RW_RENDER_STATE_DESTINATION_BLEND, oldDestinationBlend);
    renderStateSet(RW_RENDER_STATE_SOURCE_BLEND, oldSourceBlend);
    renderStateSet(RW_RENDER_STATE_VERTEX_ALPHA_ENABLE, oldVertexAlpha);
    renderStateSet(RW_RENDER_STATE_Z_WRITE_ENABLE, oldZWrite);
}

SDistantLightStats CCoronasSA::GetDistantLightStats() const
{
    return {
        g_bDistantLightsEnabled, static_cast<DWORD>(g_DistantLights.size()), static_cast<DWORD>(g_DistantLightRenderQueue.size()), MAX_DISTANT_LIGHT_CORONAS,
        GetDistantLightRange(),  g_DistantLightSettings.automaticDistance,
    };
}
