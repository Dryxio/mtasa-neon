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
#include "CRegisteredCoronaSA.h"
#include "CBuildingSA.h"
#include "CDummyPoolSA.h"
#include "CEntitySA.h"
#include "CGameSA.h"
#include "CModelInfoSA.h"
#include "CPoolsSA.h"
#include "CPoolSAInterface.h"
#include <core/CCoreInterface.h>
#include <game/CCamera.h>
#include <game/CWorld.h>
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
    // MTA consumes Project2DFX's light data through its own loader and GTA's
    // corona renderer rather than importing the ASI or its limit adjuster.
    constexpr DWORD MAX_DISTANT_LIGHT_CORONAS = 3000;
    constexpr DWORD DISTANT_LIGHT_IDENTIFIER_BASE = 0x2DF00000;
    constexpr DWORD EFFECT_LIGHT = 0;
    constexpr WORD  LIGHT_FLAG_WITHOUT_CORONA = 1 << 3;
    constexpr WORD  LIGHT_FLAG_AT_NIGHT = 1 << 6;

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
        bool       noDistance;
        bool       trafficLight;
        bool       trafficLightFacesEastWest;
    };

    struct SDistantLightDefinition
    {
        CVector localPosition;
        float   coronaSize;
        float   drawDistance;
        BYTE    red;
        BYTE    green;
        BYTE    blue;
        BYTE    alpha;
        BYTE    flashType;
        bool    noDistance;
        bool    trafficLight;
    };

    struct SDistantLightKey
    {
        int   x;
        int   y;
        int   z;
        DWORD color;

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

    bool                                                g_bDistantLightsEnabled = false;
    bool                                                g_bDistantLightsNeedRebuild = true;
    float                                               g_fDistantLightsDrawDistance = 2000.0f;
    std::vector<SDistantLight>                          g_DistantLights;
    std::unordered_map<std::size_t, CRegisteredCorona*> g_ActiveDistantLightCoronas;
    DWORD                                               g_dwDistantLightEntitiesScanned = 0;
    DWORD                                               g_dwDistantLightEffectsScanned = 0;
    DWORD                                               g_dwDistantLightEffectsFound = 0;

    using SDistantLightDefinitions = std::unordered_map<WORD, std::vector<SDistantLightDefinition>>;

    CBaseModelInfoSAInterface* GetModelInfoByName(const char* name, int* index)
    {
        return reinterpret_cast<CBaseModelInfoSAInterface*(__cdecl*)(const char*, int*)>(0x4C5940)(name, index);
    }

    S2dEffect* GetModel2dEffect(CBaseModelInfoSAInterface* modelInfo, int index)
    {
        return reinterpret_cast<S2dEffect*(__thiscall*)(CBaseModelInfoSAInterface*, int)>(0x4C4C70)(modelInfo, index);
    }

    bool ParseDistantLightDefinition(const char* line, SDistantLightDefinition& definition, bool& drawSearchlight)
    {
        unsigned int red = 0;
        unsigned int green = 0;
        unsigned int blue = 0;
        unsigned int alpha = 0;
        int          flashType = 0;
        int          noDistance = 0;
        int          searchlight = 0;

        const int fields =
            sscanf(line, "%3u %3u %3u %3u %f %f %f %f %f %2d %1d %1d", &red, &green, &blue, &alpha, &definition.localPosition.fX, &definition.localPosition.fY,
                   &definition.localPosition.fZ, &definition.coronaSize, &definition.drawDistance, &flashType, &noDistance, &searchlight);
        if (fields != 12)
        {
            definition.drawDistance = 0.0f;
            if (sscanf(line, "%3u %3u %3u %3u %f %f %f %f %2d %1d %1d", &red, &green, &blue, &alpha, &definition.localPosition.fX, &definition.localPosition.fY,
                       &definition.localPosition.fZ, &definition.coronaSize, &flashType, &noDistance, &searchlight) != 11)
                return false;
        }

        definition.red = static_cast<BYTE>(std::min(red, 255u));
        definition.green = static_cast<BYTE>(std::min(green, 255u));
        definition.blue = static_cast<BYTE>(std::min(blue, 255u));
        definition.alpha = static_cast<BYTE>(std::min(alpha, 255u));
        definition.flashType = static_cast<BYTE>(flashType > 0 && flashType <= 7 ? flashType + 18 : 0);
        definition.noDistance = noDistance != 0;
        definition.trafficLight = std::abs(definition.coronaSize - 0.45f) < 0.001f;
        drawSearchlight = searchlight != 0;
        return definition.coronaSize > 0.0f;
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
        DWORD skippedSearchlights = 0;
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
            bool                    drawSearchlight = false;
            if ((!additionalCoronas && currentModel < 0) || !ParseDistantLightDefinition(begin, definition, drawSearchlight))
                continue;

            // TODO(Project2DFX): Only 24 rows in the current SA DAT request a
            // searchlight, so phase 1 keeps their coronas and defers the cones.
            // If this is revisited, reuse CPointLightsSA::RenderHeliLight from
            // CClientPointLightsManager::RenderHeliLightHandler instead of
            // importing Project2DFX's Im3D renderer. Derive the downward cone
            // length from the model collision bounds and keep enable, distance,
            // and budget controls separate and disabled by default.
            if (drawSearchlight)
                ++skippedSearchlights;

            if (additionalCoronas)
                additionalDefinitions.push_back(definition);
            else
                modelDefinitions[static_cast<WORD>(currentModel)].push_back(definition);
        }
        fclose(file);

        std::size_t definitionCount = additionalDefinitions.size();
        for (const auto& [model, definitions] : modelDefinitions)
            definitionCount += definitions.size();
        OutputReleaseLine(SString("[Project2DFX] loaded %u DAT definitions for %u models (%u unresolved, %u searchlight cones omitted)",
                                  static_cast<DWORD>(definitionCount), namedModels, unresolvedModels, skippedSearchlights));
        return !modelDefinitions.empty() || !additionalDefinitions.empty();
    }

    void ClearActiveDistantLights()
    {
        for (const auto& [index, corona] : g_ActiveDistantLightCoronas)
        {
            if (corona)
                corona->Disable();
        }
        g_ActiveDistantLightCoronas.clear();
    }

    float SolveLinear(float a, float b, float c, float d, float value)
    {
        const float determinant = a - c;
        if (std::abs(determinant) < 0.001f)
            return d;

        const float x = (b - d) / determinant;
        const float y = (a * d - b * c) / determinant;
        return std::min(x * value + y, d);
    }

    BYTE GetNightAlpha(BYTE hour, BYTE minute)
    {
        const unsigned int time = hour * 60 + minute;
        if (time >= 20 * 60)
            return static_cast<BYTE>(std::clamp((15.0f / 16.0f) * time - 1095.0f, 0.0f, 255.0f));
        if (time < 3 * 60)
            return 255;
        return static_cast<BYTE>(std::clamp((-15.0f / 16.0f) * time + 424.0f, 0.0f, 255.0f));
    }

    bool IsDistantLightOn(BYTE flashType, std::size_t index, DWORD timeMs)
    {
        const DWORD seed = static_cast<DWORD>(index * 2654435761u);
        switch (flashType)
        {
            case 0:  // FLASH_DEFAULT
                return true;
            case 1:  // FLASH_RANDOM
            case 2:  // FLASH_RANDOM_WHEN_WET; weather dependency is omitted in phase 1
                return ((timeMs ^ seed) & 0x60) != 0 || ((seed ^ (timeMs / 4096)) & 0x3) != 0;
            case 3:  // FLASH_ANIM_SPEED_4X
                return ((timeMs + seed * 128) & 0x200) != 0;
            case 4:  // FLASH_ANIM_SPEED_2X
                return ((timeMs + seed * 256) & 0x400) != 0;
            case 5:  // FLASH_ANIM_SPEED_1X
                return ((timeMs + seed * 512) & 0x800) != 0;
            case 11:  // FLASH_5ON_5OFF
                return (timeMs + seed) % 10000 < 5000;
            case 12:  // FLASH_6ON_4OFF
                return (timeMs + seed) % 10000 < 6000;
            case 13:  // FLASH_4ON_6OFF
                return (timeMs + seed) % 10000 < 4000;
            case 19:  // Project2DFX DAT: random flashing (500 ms on/off)
                return (timeMs + seed) % 1000 < 500;
            case 20:  // Project2DFX DAT: 1 second on/off
                return (timeMs + seed) % 2000 < 1000;
            case 21:  // Project2DFX DAT: 2 seconds on/off
                return (timeMs + seed) % 4000 < 2000;
            case 22:  // Project2DFX DAT: 3 seconds on/off
                return (timeMs + seed) % 6000 < 3000;
            case 23:  // Project2DFX DAT: 4 seconds on/off
                return (timeMs + seed) % 8000 < 4000;
            case 24:  // Project2DFX DAT: 5 seconds on/off
                return (timeMs + seed) % 10000 < 5000;
            case 25:  // Project2DFX DAT: 6 seconds on, 4 seconds off
                return (timeMs + seed) % 10000 < 6000;
            default:
                return false;
        }
    }

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
                         std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen)
    {
        if (position.fZ < -15.0f || position.fZ > 1030.0f)
            return false;

        const DWORD color = static_cast<DWORD>(definition.red) | static_cast<DWORD>(definition.green) << 8 | static_cast<DWORD>(definition.blue) << 16 |
                            static_cast<DWORD>(definition.alpha) << 24;
        const SDistantLightKey key{
            static_cast<int>(std::lround(position.fX * 10.0f)),
            static_cast<int>(std::lround(position.fY * 10.0f)),
            static_cast<int>(std::lround(position.fZ * 10.0f)),
            color,
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
        });
        return true;
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
                            std::abs(worldOffsetX) > std::abs(worldOffsetY), seen);
        }
    }

    void AddNativeDistantLightsForEntity(CEntitySAInterface* entity, std::unordered_set<SDistantLightKey, SDistantLightKeyHash>& seen)
    {
        ++g_dwDistantLightEntitiesScanned;
        if (!entity || entity->m_areaCode != 0 || entity->m_nModelIndex >= pGame->GetBaseIDforTXD())
            return;

        auto* modelInfo = reinterpret_cast<CBaseModelInfoSAInterface**>(CModelInfoSAInterface::ms_modelInfoPtrs)[entity->m_nModelIndex];
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

            CVector worldPosition;
            entity->TransformFromObjectSpace(worldPosition, effect->position);
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
    g_bDistantLightsEnabled = false;
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
    if (enabled)
        g_bDistantLightsNeedRebuild = true;
    else
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
    return true;
}

void CCoronasSA::RebuildDistantLights()
{
    ClearActiveDistantLights();
    g_DistantLights.clear();
    g_dwDistantLightEntitiesScanned = 0;
    g_dwDistantLightEffectsScanned = 0;
    g_dwDistantLightEffectsFound = 0;

    std::unordered_set<SDistantLightKey, SDistantLightKeyHash> seen;
    seen.reserve(16000);

    SDistantLightDefinitions             modelDefinitions;
    std::vector<SDistantLightDefinition> additionalDefinitions;
    const bool                           loadedDat = LoadDistantLightDefinitions(modelDefinitions, additionalDefinitions);

    bool buildingPoolReady = false;
    bool dummyPoolReady = false;
    if (loadedDat)
    {
        buildingPoolReady = AddDatDistantLightsFromPool<CBuildingSAInterface>(CLASS_CBuildingPool, modelDefinitions, seen);
        dummyPoolReady = AddDatDistantLightsFromPool<CEntitySAInterface>(CLASS_CDummyPool, modelDefinitions, seen);
        for (const SDistantLightDefinition& definition : additionalDefinitions)
            AddDistantLight(definition.localPosition, definition, definition.drawDistance, false, seen);
    }
    else
    {
        buildingPoolReady = AddNativeDistantLightsFromPool<CBuildingSAInterface>(CLASS_CBuildingPool, seen);
        dummyPoolReady = AddNativeDistantLightsFromPool<CEntitySAInterface>(CLASS_CDummyPool, seen);
    }
    g_bDistantLightsNeedRebuild = !(buildingPoolReady && dummyPoolReady);

    const SString message("[Project2DFX] source=%s scanned entities=%u effects=%u lights=%u; accepted definitions=%u", loadedDat ? "DAT" : "GTA",
                          g_dwDistantLightEntitiesScanned, g_dwDistantLightEffectsScanned, g_dwDistantLightEffectsFound, g_DistantLights.size());
    OutputReleaseLine(message);
    if (g_pCore)
        g_pCore->ChatEcho(message, false);
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
    const float    farDistanceSquared = g_fDistantLightsDrawDistance * g_fDistantLightsDrawDistance;

    std::vector<SDistantLightCandidate> candidates;
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
        const float nearDistance = light.noDistance ? 0.0f : std::max(0.0f, light.objectDrawDistance - 30.0f);
        if ((light.noDistance || distanceSquared > nearDistance * nearDistance) && distanceSquared < farDistanceSquared)
            candidates.push_back({distanceSquared, i});
    }

    if (candidates.size() > MAX_DISTANT_LIGHT_CORONAS)
    {
        std::nth_element(candidates.begin(), candidates.begin() + MAX_DISTANT_LIGHT_CORONAS, candidates.end(),
                         [](const SDistantLightCandidate& left, const SDistantLightCandidate& right) { return left.distanceSquared < right.distanceSquared; });
        candidates.resize(MAX_DISTANT_LIGHT_CORONAS);
    }

    std::unordered_set<std::size_t> selected;
    selected.reserve(candidates.size());
    for (const SDistantLightCandidate& candidate : candidates)
        selected.insert(candidate.index);

    for (auto iter = g_ActiveDistantLightCoronas.begin(); iter != g_ActiveDistantLightCoronas.end();)
    {
        if (!selected.contains(iter->first))
        {
            iter->second->Disable();
            iter = g_ActiveDistantLightCoronas.erase(iter);
        }
        else
            ++iter;
    }

    const BYTE  nightAlpha = GetNightAlpha(hour, minute);
    const DWORD timeMs = *reinterpret_cast<DWORD*>(0xB7CB7C);
    for (const SDistantLightCandidate& candidate : candidates)
    {
        const std::size_t    index = candidate.index;
        const SDistantLight& light = g_DistantLights[index];
        const float          distance = std::sqrt(candidate.distanceSquared);
        const float          nearDistance = light.noDistance ? 0.0f : std::max(0.0f, light.objectDrawDistance - 30.0f);

        float radius = light.noDistance ? 1.75f : SolveLinear(nearDistance, 0.0f, std::max(light.objectDrawDistance, nearDistance + 1.0f), 1.75f, distance);
        radius *= std::min(SolveLinear(nearDistance, 1.0f, 1000.0f, 4.0f, distance), 4.0f);
        const float finalRadius = radius * light.coronaSize * 0.5f;

        float alphaMultiplier = light.noDistance ? 1.0f : std::clamp((distance - nearDistance) / 30.0f, 0.0f, 1.0f);
        if (distance > g_fDistantLightsDrawDistance - 100.0f)
            alphaMultiplier *= std::clamp((g_fDistantLightsDrawDistance - distance) / 100.0f, 0.0f, 1.0f);

        const float distanceFromFadeStart = distance - nearDistance;
        float       distanceAlpha = 0.5f + std::clamp(distanceFromFadeStart / 150.0f, 0.0f, 1.0f) * 0.5f;
        if (distanceFromFadeStart > 150.0f)
            distanceAlpha = 1.0f + std::clamp((distanceFromFadeStart - 150.0f) / 900.0f, 0.0f, 1.0f) * 3.0f;

        const float radiusAlpha = finalRadius > 1.0f ? std::clamp(1.0f / (0.75f * finalRadius + 0.25f), 0.3f, 1.0f) : 1.0f;
        BYTE        alpha = static_cast<BYTE>(
            std::clamp((nightAlpha / 255.0f) * (light.alpha / 255.0f) * alphaMultiplier * distanceAlpha * radiusAlpha * 255.0f, 0.0f, 255.0f));
        if (!IsDistantLightOn(light.flashType, index, timeMs))
            alpha = 0;

        CRegisteredCorona* corona = nullptr;
        auto               active = g_ActiveDistantLightCoronas.find(index);
        if (active != g_ActiveDistantLightCoronas.end())
            corona = active->second;
        else
        {
            CVector position = light.position;
            corona = CreateCorona(DISTANT_LIGHT_IDENTIFIER_BASE + static_cast<DWORD>(index + 1), &position);
            if (!corona)
                continue;
            g_ActiveDistantLightCoronas.emplace(index, corona);
        }

        CVector position = light.position;
        corona->SetPosition(&position);
        if (light.texture)
            corona->SetTexture(light.texture);
        corona->SetSize(finalRadius);
        corona->SetRange(g_fDistantLightsDrawDistance);
        corona->SetPullTowardsCamera(0.0f);
        corona->SetColor(light.red, light.green, light.blue, alpha);
        corona->SetFlareType(light.flareType);
        corona->SetReflectionType(0);
        corona->Refresh();
    }
}

SDistantLightStats CCoronasSA::GetDistantLightStats() const
{
    return {
        g_bDistantLightsEnabled,
        static_cast<DWORD>(g_DistantLights.size()),
        static_cast<DWORD>(g_ActiveDistantLightCoronas.size()),
        MAX_DISTANT_LIGHT_CORONAS,
        g_fDistantLightsDrawDistance,
    };
}
