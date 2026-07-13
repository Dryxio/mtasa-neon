/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        multiplayer_sa/multiplayer_keysync.cpp
 *  PURPOSE:     Multiplayer module keysync methods
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"

#include <game/CWeaponStatManager.h>

#include "../game_sa/CColModelSA.h"
#include "../game_sa/CColPointSA.h"
#include "../game_sa/CPhysicalSA.h"

extern CMultiplayerSA* pMultiplayer;
extern CCoreInterface* g_pCore;

DWORD dwCurrentPlayerPed = 0;  // stores the player ped temporarily during hooks
DWORD dwCurrentVehicle = 0;    // stores the current vehicle during the hooks

DWORD dwParameter = 0;

BOOL bRadioHackInstalled = FALSE;
bool b1stPersonWeaponModeHackInPlace = false;
bool bNotInLocalContext = false;
bool bMouseLookEnabled = true;
bool bInfraredVisionEnabled = false;
bool bNightVisionEnabled = false;

extern CStatsData localStatsData;
extern bool       bLocalStatsStatic;
extern float      fLocalPlayerCameraRotation;
extern bool       bCustomCameraRotation;
extern float      fGlobalGravity;
extern float      fLocalPlayerGravity;

extern PreContextSwitchHandler*  m_pPreContextSwitchHandler;
extern PostContextSwitchHandler* m_pPostContextSwitchHandler;

#define NUM_FirstStreamEngineSlot     7
#define NUM_LastStreamEngineSlot      16
#define NUM_LocalVehicleAudioContext  0x0
#define NUM_RemoteVehicleAudioContext 0x1
#define VAR_VehicleAudioContext       0x50230C

namespace
{
    enum class EEntityPerformanceType : uint8
    {
        NONE,
        VEHICLE,
        PED,
    };

    struct SEntityPerformanceStats
    {
        uint32 collisionCalls{};
        TIMEUS collisionTimeUs{};
        uint32 uniqueEntities{};
        uint32 unsafeAfterAttempt[6]{};

        uint32 collisionStepCalls{};
        uint32 collisionStepsTotal{};
        uint32 collisionStepsMax{};

        uint32 sectorCalls{};
        TIMEUS sectorTimeUs{};
        uint32 shiftSectorCalls{};
        TIMEUS shiftSectorTimeUs{};
        uint32 shiftCalls{};
        TIMEUS shiftTimeUs{};

        uint32 broadPhaseEntries{};
        uint32 broadPhaseSphereTests{};
        uint32 broadPhaseSpherePasses{};

        uint32 processEntityCalls{};
        TIMEUS processEntityTimeUs{};
        uint32 contactsProduced{};

        uint32 processColModelsCalls{};
        TIMEUS processColModelsTimeUs{};
        uint32 repeatedProcessColModelsQueries{};
    };

    struct SEntityAttemptRecord
    {
        CPhysicalSAInterface*  pEntity{};
        EEntityPerformanceType type{EEntityPerformanceType::NONE};
        uint32                 calls{};
    };

    struct SProcessColModelsQuery
    {
        EEntityPerformanceType type{EEntityPerformanceType::NONE};
        CPhysicalSAInterface*  pEntity{};
        CEntitySAInterface*    pCandidate{};
        const void*            pColModelA{};
        const void*            pColModelB{};
        uint64                 colDataStateA{};
        uint64                 colDataStateB{};
        std::array<uint32, 16> matrixA{};
        std::array<uint32, 16> matrixB{};
        bool                   hasLineOutputs{};
        bool                   returnAllCollisions{};

        bool operator==(const SProcessColModelsQuery& other) const
        {
            return type == other.type && pEntity == other.pEntity && pCandidate == other.pCandidate && pColModelA == other.pColModelA &&
                   pColModelB == other.pColModelB && colDataStateA == other.colDataStateA && colDataStateB == other.colDataStateB && matrixA == other.matrixA &&
                   matrixB == other.matrixB && hasLineOutputs == other.hasLineOutputs && returnAllCollisions == other.returnAllCollisions;
        }
    };

    struct SProcessColModelsQueryHash
    {
        size_t operator()(const SProcessColModelsQuery& query) const
        {
            size_t     hash = 2166136261u;
            const auto mix = [&hash](size_t value)
            {
                hash ^= value;
                hash *= 16777619u;
            };

            mix(static_cast<size_t>(query.type));
            mix(reinterpret_cast<size_t>(query.pEntity));
            mix(reinterpret_cast<size_t>(query.pCandidate));
            mix(reinterpret_cast<size_t>(query.pColModelA));
            mix(reinterpret_cast<size_t>(query.pColModelB));
            mix(static_cast<size_t>(query.colDataStateA));
            mix(static_cast<size_t>(query.colDataStateA >> 32));
            mix(static_cast<size_t>(query.colDataStateB));
            mix(static_cast<size_t>(query.colDataStateB >> 32));
            for (uint32 value : query.matrixA)
                mix(value);
            for (uint32 value : query.matrixB)
                mix(value);
            mix(query.hasLineOutputs);
            mix(query.returnAllCollisions);
            return hash;
        }
    };

    bool                                                                   g_entityPerformanceFrameActive{};
    EEntityPerformanceType                                                 g_activeEntityPerformanceType{EEntityPerformanceType::NONE};
    CPhysicalSAInterface*                                                  g_activeCollisionEntity{};
    CEntitySAInterface*                                                    g_activeCollisionCandidate{};
    TIMEUS                                                                 g_collisionAttemptStartUs{};
    uint32                                                                 g_collisionAttemptOrdinal{};
    std::array<SEntityPerformanceStats, 2>                                 g_entityPerformanceStats{};
    std::vector<SEntityAttemptRecord>                                      g_entityAttemptRecords;
    std::unordered_set<SProcessColModelsQuery, SProcessColModelsQueryHash> g_processColModelsQueries;

    SEntityPerformanceStats* GetEntityPerformanceStats(EEntityPerformanceType type)
    {
        switch (type)
        {
            case EEntityPerformanceType::VEHICLE:
                return &g_entityPerformanceStats[0];
            case EEntityPerformanceType::PED:
                return &g_entityPerformanceStats[1];
            default:
                return nullptr;
        }
    }

    void BeginCollisionAttempt(CPhysicalSAInterface* pEntity, EEntityPerformanceType type)
    {
        if (!g_entityPerformanceFrameActive)
            return;

        SEntityPerformanceStats* pStats = GetEntityPerformanceStats(type);
        if (!pStats)
            return;

        auto iter = std::find_if(g_entityAttemptRecords.begin(), g_entityAttemptRecords.end(),
                                 [pEntity](const SEntityAttemptRecord& record) { return record.pEntity == pEntity; });
        if (iter == g_entityAttemptRecords.end())
        {
            g_entityAttemptRecords.push_back({pEntity, type, 1});
            g_collisionAttemptOrdinal = 0;
            pStats->uniqueEntities++;
        }
        else
        {
            g_collisionAttemptOrdinal = iter->calls++;
        }

        pStats->collisionCalls++;
        g_activeEntityPerformanceType = type;
        g_activeCollisionEntity = pEntity;
        g_collisionAttemptStartUs = GetTimeUs();
    }

    void EndCollisionAttempt()
    {
        if (!g_entityPerformanceFrameActive || !g_activeCollisionEntity)
            return;

        if (SEntityPerformanceStats* pStats = GetEntityPerformanceStats(g_activeEntityPerformanceType))
        {
            pStats->collisionTimeUs += GetTimeUs() - g_collisionAttemptStartUs;
            if (!g_activeCollisionEntity->bIsInSafePosition)
                pStats->unsafeAfterAttempt[std::min<uint32>(g_collisionAttemptOrdinal, 5)]++;
        }

        g_activeEntityPerformanceType = EEntityPerformanceType::NONE;
        g_activeCollisionEntity = nullptr;
        g_activeCollisionCandidate = nullptr;
    }

    void RecordCollisionSteps(EEntityPerformanceType type, uint8 steps)
    {
        if (!g_entityPerformanceFrameActive)
            return;

        if (SEntityPerformanceStats* pStats = GetEntityPerformanceStats(type))
        {
            pStats->collisionStepCalls++;
            pStats->collisionStepsTotal += steps;
            pStats->collisionStepsMax = std::max<uint32>(pStats->collisionStepsMax, steps);
        }
    }

    void OutputEntityPerformanceStats(const char* name, const SEntityPerformanceStats& stats)
    {
        if (!stats.collisionCalls && !stats.shiftCalls)
            return;

        const uint32 retries = stats.collisionCalls > stats.uniqueEntities ? stats.collisionCalls - stats.uniqueEntities : 0;
        const double averageSteps = stats.collisionStepCalls ? static_cast<double>(stats.collisionStepsTotal) / stats.collisionStepCalls : 0.0;

        TIMING_DETAIL(
            SString("Entity collision %s: collision=%u/%lluus entities=%u retries=%u unsafe=[%u,%u,%u,%u,%u,%u] steps=%.2f/max%u "
                    "sector=%u/%lluus shift-sector=%u/%lluus shift=%u/%lluus broad=%u/%u/%u entity=%u/%lluus contacts=%u colmodels=%u/%lluus "
                    "repeated=%u",
                    name, stats.collisionCalls, static_cast<unsigned long long>(stats.collisionTimeUs), stats.uniqueEntities, retries,
                    stats.unsafeAfterAttempt[0], stats.unsafeAfterAttempt[1], stats.unsafeAfterAttempt[2], stats.unsafeAfterAttempt[3],
                    stats.unsafeAfterAttempt[4], stats.unsafeAfterAttempt[5], averageSteps, stats.collisionStepsMax, stats.sectorCalls,
                    static_cast<unsigned long long>(stats.sectorTimeUs), stats.shiftSectorCalls, static_cast<unsigned long long>(stats.shiftSectorTimeUs),
                    stats.shiftCalls, static_cast<unsigned long long>(stats.shiftTimeUs), stats.broadPhaseEntries, stats.broadPhaseSphereTests,
                    stats.broadPhaseSpherePasses, stats.processEntityCalls, static_cast<unsigned long long>(stats.processEntityTimeUs), stats.contactsProduced,
                    stats.processColModelsCalls, static_cast<unsigned long long>(stats.processColModelsTimeUs), stats.repeatedProcessColModelsQueries));
    }

    void BeginPlayerPedProcessControlTiming()
    {
        TIMING_CHECKPOINT("+GTA_PlayerPedProcessControl");
    }
    void EndPlayerPedProcessControlTiming()
    {
        TIMING_CHECKPOINT("-GTA_PlayerPedProcessControl");
    }
    void BeginPlayerPedProcessCollisionTiming(CPhysicalSAInterface* pPhysical)
    {
        TIMING_CHECKPOINT("+GTA_PlayerPedProcessCollision");
        BeginCollisionAttempt(pPhysical, EEntityPerformanceType::PED);
    }
    void EndPlayerPedProcessCollisionTiming()
    {
        EndCollisionAttempt();
        TIMING_CHECKPOINT("-GTA_PlayerPedProcessCollision");
    }
    void BeginPlayerPedPreRenderTiming()
    {
        TIMING_CHECKPOINT("+GTA_PlayerPedPreRender");
    }
    void EndPlayerPedPreRenderTiming()
    {
        TIMING_CHECKPOINT("-GTA_PlayerPedPreRender");
    }
    void BeginAutomobileProcessControlTiming()
    {
        TIMING_CHECKPOINT("+GTA_AutomobileProcessControl");
    }
    void EndAutomobileProcessControlTiming()
    {
        TIMING_CHECKPOINT("-GTA_AutomobileProcessControl");
    }
    void BeginAutomobileProcessCollisionTiming(CPhysicalSAInterface* pPhysical)
    {
        TIMING_CHECKPOINT("+GTA_AutomobileProcessCollision");
        BeginCollisionAttempt(pPhysical, EEntityPerformanceType::VEHICLE);
    }
    void EndAutomobileProcessCollisionTiming()
    {
        EndCollisionAttempt();
        TIMING_CHECKPOINT("-GTA_AutomobileProcessCollision");
    }
    void BeginAutomobilePreRenderTiming()
    {
        TIMING_CHECKPOINT("+GTA_AutomobilePreRender");
    }
    void EndAutomobilePreRenderTiming()
    {
        TIMING_CHECKPOINT("-GTA_AutomobilePreRender");
    }

    int32 __fastcall ProcessEntityCollisionWithTiming(CPhysicalSAInterface* pPhysical, void*, CEntitySAInterface* pEntity, CColPointSAInterface* pColPoints,
                                                      EEntityPerformanceType type, DWORD functionAddress)
    {
        if (!g_entityPerformanceFrameActive)
            return reinterpret_cast<int32(__thiscall*)(CPhysicalSAInterface*, CEntitySAInterface*, CColPointSAInterface*)>(functionAddress)(pPhysical, pEntity,
                                                                                                                                            pColPoints);

        SEntityPerformanceStats* pStats = GetEntityPerformanceStats(type);
        const TIMEUS             startUs = GetTimeUs();
        CPhysicalSAInterface*    previousEntity = g_activeCollisionEntity;
        CEntitySAInterface*      previousCandidate = g_activeCollisionCandidate;
        EEntityPerformanceType   previousType = g_activeEntityPerformanceType;
        g_activeCollisionEntity = pPhysical;
        g_activeCollisionCandidate = pEntity;
        g_activeEntityPerformanceType = type;

        const int32 contacts = reinterpret_cast<int32(__thiscall*)(CPhysicalSAInterface*, CEntitySAInterface*, CColPointSAInterface*)>(functionAddress)(
            pPhysical, pEntity, pColPoints);

        pStats->processEntityCalls++;
        pStats->processEntityTimeUs += GetTimeUs() - startUs;
        if (contacts > 0)
            pStats->contactsProduced += contacts;

        g_activeCollisionEntity = previousEntity;
        g_activeCollisionCandidate = previousCandidate;
        g_activeEntityPerformanceType = previousType;
        return contacts;
    }

    int32 __fastcall HOOK_CAutomobile__ProcessEntityCollisionTiming(CPhysicalSAInterface* pPhysical, void* pUnused, CEntitySAInterface* pEntity,
                                                                    CColPointSAInterface* pColPoints)
    {
        return ProcessEntityCollisionWithTiming(pPhysical, pUnused, pEntity, pColPoints, EEntityPerformanceType::VEHICLE, 0x6ACE70);
    }

    int32 __fastcall HOOK_CPlayerPed__ProcessEntityCollisionTiming(CPhysicalSAInterface* pPhysical, void* pUnused, CEntitySAInterface* pEntity,
                                                                   CColPointSAInterface* pColPoints)
    {
        return ProcessEntityCollisionWithTiming(pPhysical, pUnused, pEntity, pColPoints, EEntityPerformanceType::PED, 0x5E2530);
    }

    void __fastcall ProcessShiftWithTiming(CPhysicalSAInterface* pPhysical, EEntityPerformanceType type)
    {
        const bool               enabled = g_entityPerformanceFrameActive;
        SEntityPerformanceStats* pStats = enabled ? GetEntityPerformanceStats(type) : nullptr;
        const TIMEUS             startUs = enabled ? GetTimeUs() : 0;
        const auto               previousType = g_activeEntityPerformanceType;
        CPhysicalSAInterface*    previousEntity = g_activeCollisionEntity;
        if (enabled)
        {
            g_activeEntityPerformanceType = type;
            g_activeCollisionEntity = pPhysical;
        }

        reinterpret_cast<void(__thiscall*)(CPhysicalSAInterface*)>(0x54DB10)(pPhysical);

        if (enabled)
        {
            pStats->shiftCalls++;
            pStats->shiftTimeUs += GetTimeUs() - startUs;
            g_activeEntityPerformanceType = previousType;
            g_activeCollisionEntity = previousEntity;
        }
    }

    void __fastcall HOOK_CAutomobile__ProcessShiftTiming(CPhysicalSAInterface* pPhysical, void*)
    {
        ProcessShiftWithTiming(pPhysical, EEntityPerformanceType::VEHICLE);
    }

    void __fastcall HOOK_CPlayerPed__ProcessShiftTiming(CPhysicalSAInterface* pPhysical, void*)
    {
        ProcessShiftWithTiming(pPhysical, EEntityPerformanceType::PED);
    }

    uint8 __fastcall CollisionStepsWithTiming(CPhysicalSAInterface* pPhysical, bool& processBeforeTimeStep, bool& unknown, EEntityPerformanceType type,
                                              DWORD functionAddress)
    {
        const uint8 steps =
            reinterpret_cast<uint8(__thiscall*)(CPhysicalSAInterface*, bool&, bool&)>(functionAddress)(pPhysical, processBeforeTimeStep, unknown);
        RecordCollisionSteps(type, steps);
        return steps;
    }

    uint8 __fastcall HOOK_CAutomobile__CollisionStepsTiming(CPhysicalSAInterface* pPhysical, void*, bool& processBeforeTimeStep, bool& unknown)
    {
        return CollisionStepsWithTiming(pPhysical, processBeforeTimeStep, unknown, EEntityPerformanceType::VEHICLE, 0x6D0E90);
    }

    uint8 __fastcall HOOK_CPlayerPed__CollisionStepsTiming(CPhysicalSAInterface* pPhysical, void*, bool& processBeforeTimeStep, bool& unknown)
    {
        return CollisionStepsWithTiming(pPhysical, processBeforeTimeStep, unknown, EEntityPerformanceType::PED, 0x5E3E90);
    }

    bool __fastcall ProcessCollisionSectorListWithTiming(CPhysicalSAInterface* pPhysical, void*, int32 sectorX, int32 sectorY, bool shift)
    {
        if (!g_entityPerformanceFrameActive)
            return reinterpret_cast<bool(__thiscall*)(CPhysicalSAInterface*, int32, int32)>(0x54BA60)(pPhysical, sectorX, sectorY);

        const TIMEUS startUs = GetTimeUs();
        const bool   result = reinterpret_cast<bool(__thiscall*)(CPhysicalSAInterface*, int32, int32)>(0x54BA60)(pPhysical, sectorX, sectorY);
        if (SEntityPerformanceStats* pStats = GetEntityPerformanceStats(g_activeEntityPerformanceType))
        {
            if (shift)
            {
                pStats->shiftSectorCalls++;
                pStats->shiftSectorTimeUs += GetTimeUs() - startUs;
            }
            else
            {
                pStats->sectorCalls++;
                pStats->sectorTimeUs += GetTimeUs() - startUs;
            }
        }
        return result;
    }

    bool __fastcall HOOK_CPhysical__ProcessCollisionSectorListTiming(CPhysicalSAInterface* pPhysical, void* pUnused, int32 sectorX, int32 sectorY)
    {
        return ProcessCollisionSectorListWithTiming(pPhysical, pUnused, sectorX, sectorY, false);
    }

    bool __fastcall HOOK_CPhysical__ProcessShiftCollisionSectorListTiming(CPhysicalSAInterface* pPhysical, void* pUnused, int32 sectorX, int32 sectorY)
    {
        return ProcessCollisionSectorListWithTiming(pPhysical, pUnused, sectorX, sectorY, true);
    }

    bool __fastcall HOOK_CEntity__GetIsTouchingCollisionTiming(CEntitySAInterface* pEntity, void*, const CVector& centre, float radius)
    {
        SEntityPerformanceStats* pStats = g_entityPerformanceFrameActive ? GetEntityPerformanceStats(g_activeEntityPerformanceType) : nullptr;
        if (pStats)
            pStats->broadPhaseSphereTests++;

        const bool touching = reinterpret_cast<bool(__thiscall*)(CEntitySAInterface*, const CVector&, float)>(0x5344B0)(pEntity, centre, radius);
        if (pStats && touching)
            pStats->broadPhaseSpherePasses++;
        return touching;
    }

    uint64 ReadColDataState(const CColModelSAInterface* pColModel)
    {
        if (!pColModel || !pColModel->m_data)
            return 0;

        uint64 state{};
        MemCpyFast(&state, pColModel->m_data, sizeof(state));
        return state;
    }

    int32 __cdecl HOOK_CCollision__ProcessColModelsTiming(const void* pMatrixA, CColModelSAInterface* pColModelA, const void* pMatrixB,
                                                          CColModelSAInterface* pColModelB, CColPointSAInterface* pSpherePoints,
                                                          CColPointSAInterface* pLinePoints, float* pMaxTouchDistances, bool returnAllCollisions)
    {
        SEntityPerformanceStats* pStats = g_entityPerformanceFrameActive ? GetEntityPerformanceStats(g_activeEntityPerformanceType) : nullptr;
        if (!pStats)
        {
            return reinterpret_cast<int32(__cdecl*)(const void*, CColModelSAInterface*, const void*, CColModelSAInterface*, CColPointSAInterface*,
                                                    CColPointSAInterface*, float*, bool)>(0x4185C0)(pMatrixA, pColModelA, pMatrixB, pColModelB, pSpherePoints,
                                                                                                    pLinePoints, pMaxTouchDistances, returnAllCollisions);
        }

        SProcessColModelsQuery query;
        query.type = g_activeEntityPerformanceType;
        query.pEntity = g_activeCollisionEntity;
        query.pCandidate = g_activeCollisionCandidate;
        query.pColModelA = pColModelA;
        query.pColModelB = pColModelB;
        query.colDataStateA = ReadColDataState(pColModelA);
        query.colDataStateB = ReadColDataState(pColModelB);
        query.hasLineOutputs = pLinePoints && pMaxTouchDistances;
        query.returnAllCollisions = returnAllCollisions;
        MemCpyFast(query.matrixA.data(), pMatrixA, sizeof(query.matrixA));
        MemCpyFast(query.matrixB.data(), pMatrixB, sizeof(query.matrixB));

        if (!g_processColModelsQueries.emplace(query).second)
            pStats->repeatedProcessColModelsQueries++;

        const TIMEUS startUs = GetTimeUs();
        const int32  contacts = reinterpret_cast<int32(__cdecl*)(const void*, CColModelSAInterface*, const void*, CColModelSAInterface*, CColPointSAInterface*,
                                                                CColPointSAInterface*, float*, bool)>(0x4185C0)(
            pMatrixA, pColModelA, pMatrixB, pColModelB, pSpherePoints, pLinePoints, pMaxTouchDistances, returnAllCollisions);
        pStats->processColModelsCalls++;
        pStats->processColModelsTimeUs += GetTimeUs() - startUs;
        return contacts;
    }

    // These virtual hooks aggregate native work by entity category in the
    // opt-in timing log. Calling the verified GTA functions directly preserves
    // the original vtable semantics while avoiding per-entity log messages.
    static void __declspec(naked) HOOK_CPlayerPed__ProcessCollisionTiming()
    {
        MTA_VERIFY_HOOK_LOCAL_SIZE;

        // clang-format off
        __asm
        {
            pushad
            push    ecx
            call    BeginPlayerPedProcessCollisionTiming
            add     esp, 4
            popad
            mov     eax, FUNC_CPlayerPed__ProcessCollision
            call    eax
            pushad
            call    EndPlayerPedProcessCollisionTiming
            popad
            retn
        }
        // clang-format on
    }

    static void __declspec(naked) HOOK_CPlayerPed__PreRenderTiming()
    {
        MTA_VERIFY_HOOK_LOCAL_SIZE;

        // clang-format off
        __asm
        {
            pushad
            call    BeginPlayerPedPreRenderTiming
            popad
            mov     eax, FUNC_CPlayerPed__PreRender
            call    eax
            pushad
            call    EndPlayerPedPreRenderTiming
            popad
            retn
        }
        // clang-format on
    }

    static void __declspec(naked) HOOK_CAutomobile__ProcessCollisionTiming()
    {
        MTA_VERIFY_HOOK_LOCAL_SIZE;

        // clang-format off
        __asm
        {
            pushad
            push    ecx
            call    BeginAutomobileProcessCollisionTiming
            add     esp, 4
            popad
            mov     eax, FUNC_CAutomobile__ProcessCollision
            call    eax
            pushad
            call    EndAutomobileProcessCollisionTiming
            popad
            retn
        }
        // clang-format on
    }

    static void __declspec(naked) HOOK_CAutomobile__PreRenderTiming()
    {
        MTA_VERIFY_HOOK_LOCAL_SIZE;

        // clang-format off
        __asm
        {
            pushad
            call    BeginAutomobilePreRenderTiming
            popad
            mov     eax, FUNC_CAutomobile__PreRender
            call    eax
            pushad
            call    EndAutomobilePreRenderTiming
            popad
            retn
        }
        // clang-format on
    }

    bool HasValidVehicleAudioContext(const CAEVehicleAudioEntitySAInterface* pAudioInterface) noexcept
    {
        if (!pAudioInterface)
            return false;

        if (pAudioInterface->m_wEngineBankSlotId >= NUM_FirstStreamEngineSlot && pAudioInterface->m_wEngineBankSlotId <= NUM_LastStreamEngineSlot)
            return true;

        return pAudioInterface->m_wEngineAccelerateSoundBankId >= 0 || pAudioInterface->m_wEngineDecelerateSoundBankId >= 0;
    }

    void SetVehicleAudioContext(CVehicleSA* pVehicleSA, BYTE ucContext)
    {
        if (ucContext == NUM_LocalVehicleAudioContext && pVehicleSA)
        {
            auto* pVehicleAudioEntity = pVehicleSA->GetVehicleAudioEntity();
            auto* pAudioInterface = pVehicleAudioEntity ? pVehicleAudioEntity->GetInterface() : nullptr;
            if (!HasValidVehicleAudioContext(pAudioInterface))
                return;
        }

        if (*reinterpret_cast<BYTE*>(VAR_VehicleAudioContext) == ucContext)
            return;

        MemPutFast<BYTE>(VAR_VehicleAudioContext, ucContext);
    }
}

void EntityPerformanceBeginWorldFrame()
{
    g_entityPerformanceFrameActive = IS_TIMING_CHECKPOINTS();
    g_activeEntityPerformanceType = EEntityPerformanceType::NONE;
    g_activeCollisionEntity = nullptr;
    g_activeCollisionCandidate = nullptr;
    g_collisionAttemptStartUs = 0;
    g_collisionAttemptOrdinal = 0;

    if (!g_entityPerformanceFrameActive)
        return;

    g_entityPerformanceStats = {};
    g_entityAttemptRecords.clear();
    g_processColModelsQueries.clear();
    if (!g_entityAttemptRecords.capacity())
        g_entityAttemptRecords.reserve(256);
    g_processColModelsQueries.reserve(1024);
}

int32 __cdecl EntityPerformanceProcessColModels(const void* pMatrixA, CColModelSAInterface* pColModelA, const void* pMatrixB, CColModelSAInterface* pColModelB,
                                                CColPointSAInterface* pSpherePoints, CColPointSAInterface* pLinePoints, float* pMaxTouchDistances,
                                                bool returnAllCollisions)
{
    return HOOK_CCollision__ProcessColModelsTiming(pMatrixA, pColModelA, pMatrixB, pColModelB, pSpherePoints, pLinePoints, pMaxTouchDistances,
                                                   returnAllCollisions);
}

void EntityPerformanceEndWorldFrame()
{
    if (!g_entityPerformanceFrameActive)
        return;

    OutputEntityPerformanceStats("vehicle", g_entityPerformanceStats[0]);
    OutputEntityPerformanceStats("ped", g_entityPerformanceStats[1]);
    g_entityPerformanceFrameActive = false;
    g_activeEntityPerformanceType = EEntityPerformanceType::NONE;
    g_activeCollisionEntity = nullptr;
    g_activeCollisionCandidate = nullptr;
}

void EntityPerformanceRecordBroadPhaseCandidate(CPhysicalSAInterface* pPhysical, CPhysicalSAInterface*)
{
    if (!g_entityPerformanceFrameActive || pPhysical != g_activeCollisionEntity)
        return;

    if (SEntityPerformanceStats* pStats = GetEntityPerformanceStats(g_activeEntityPerformanceType))
        pStats->broadPhaseEntries++;
}

VOID InitKeysyncHooks()
{
    // OutputDebugString("InitKeysyncHooks");
    HookInstallMethod(VTBL_CPlayerPed__ProcessControl, (DWORD)HOOK_CPlayerPed__ProcessControl);
    HookInstallMethod(VTBL_CAutomobile__ProcessControl, (DWORD)HOOK_CAutomobile__ProcessControl);
    HookInstallMethod(VTBL_CMonsterTruck__ProcessControl, (DWORD)HOOK_CMonsterTruck__ProcessControl);
    HookInstallMethod(VTBL_CTrailer__ProcessControl, (DWORD)HOOK_CTrailer__ProcessControl);
    HookInstallMethod(VTBL_CQuadBike__ProcessControl, (DWORD)HOOK_CQuadBike__ProcessControl);
    HookInstallMethod(VTBL_CPlane__ProcessControl, (DWORD)HOOK_CPlane__ProcessControl);
    HookInstallMethod(VTBL_CBmx__ProcessControl, (DWORD)HOOK_CBmx__ProcessControl);
    HookInstallMethod(VTBL_CTrain__ProcessControl, (DWORD)HOOK_CTrain__ProcessControl);
    HookInstallMethod(VTBL_CBoat__ProcessControl, (DWORD)HOOK_CBoat__ProcessControl);
    HookInstallMethod(VTBL_CBike__ProcessControl, (DWORD)HOOK_CBike__ProcessControl);
    HookInstallMethod(VTBL_CHeli__ProcessControl, (DWORD)HOOK_CHeli__ProcessControl);
    HookInstallMethod(VTBL_CPlayerPed__ProcessCollision, (DWORD)HOOK_CPlayerPed__ProcessCollisionTiming);
    HookInstallMethod(VTBL_CPlayerPed__PreRender, (DWORD)HOOK_CPlayerPed__PreRenderTiming);
    HookInstallMethod(VTBL_CAutomobile__ProcessCollision, (DWORD)HOOK_CAutomobile__ProcessCollisionTiming);
    HookInstallMethod(VTBL_CAutomobile__PreRender, (DWORD)HOOK_CAutomobile__PreRenderTiming);
    HookInstallMethod(0x86D198, (DWORD)HOOK_CPlayerPed__ProcessShiftTiming);
    HookInstallMethod(0x871150, (DWORD)HOOK_CAutomobile__ProcessShiftTiming);
    HookInstallMethod(0x86D1A8, (DWORD)HOOK_CPlayerPed__CollisionStepsTiming);
    HookInstallMethod(0x871160, (DWORD)HOOK_CAutomobile__CollisionStepsTiming);
    HookInstallMethod(0x86D1C0, (DWORD)HOOK_CPlayerPed__ProcessEntityCollisionTiming);
    HookInstallMethod(0x871178, (DWORD)HOOK_CAutomobile__ProcessEntityCollisionTiming);

    // These verified GTA SA 1.0 US call sites split sector traversal from the
    // virtual narrow phase while leaving the original functions intact.
    HookInstallCall(0x54DA84, (DWORD)HOOK_CPhysical__ProcessCollisionSectorListTiming);
    HookInstallCall(0x54DDA4, (DWORD)HOOK_CPhysical__ProcessShiftCollisionSectorListTiming);
    HookInstallCall(0x54BBD2, (DWORD)HOOK_CEntity__GetIsTouchingCollisionTiming);
    HookInstallCall(0x546D56, (DWORD)HOOK_CCollision__ProcessColModelsTiming);
    HookInstallCall(0x5E2837, (DWORD)HOOK_CCollision__ProcessColModelsTiming);
    HookInstallCall(0x5E3127, (DWORD)HOOK_CCollision__ProcessColModelsTiming);

    // not strictly for keysync, to make CPlayerPed::GetPlayerInfoForThisPlayerPed always return the local playerinfo
    // 00609FF2     EB 1F          JMP SHORT gta_sa_u.0060A013
    MemSet((void*)0x609FF2, 0xEB, 1);
    MemSet((void*)0x609FF3, 0x1F, 1);
    MemSet((void*)0x609FF4, 0x90, 1);
    MemSet((void*)0x609FF5, 0x90, 1);
    MemSet((void*)0x609FF6, 0x90, 1);

    // and this is to fix bike sync (I hope)
    // 006BC9EB   9090               NOP NOP
    MemSet((void*)0x6BC9EB, 0x90, 2);
}

extern CPed* pContextSwitchedPed;
void         PostContextSwitch()
{
    // Prevent the game making remote player's weapons get switched by the local player's
    MemPutFast<BYTE>(0x60D850, 0x56);
    MemPutFast<BYTE>(0x60D851, 0x57);
    MemPutFast<BYTE>(0x60D852, 0x8B);

    // Prevent it calling ClearWeaponTarget for remote players
    MemPutFast<BYTE>(0x609C80, 0x57);

    // Prevent CCamera::SetNewPlayerWeaponMode being called
    MemPutFast<BYTE>(0x50BFB0, 0x66);
    MemPutFast<BYTE>(0x50BFB1, 0x8B);
    MemPutFast<BYTE>(0x50BFB2, 0x44);

    // This is so weapon clicks and similar don't play for us when done remotely
    MemPutFast<BYTE>(0x60F273, 0x75);
    MemPutFast<BYTE>(0x60F260, 0x74);
    MemPutFast<BYTE>(0x60F261, 0x13);

    // Prevent it calling CCamera::ClearPlayerWeaponMode for remote players
    MemPutFast<BYTE>(0x50AB10, 0x33);

    // this is to prevent shooting players following the local camera
    MemPutFast<BYTE>(0x687099, 0x75);

    // Prevent rockets firing oddly
    //*(BYTE *)0x73811C = 0x0F;
    //*(BYTE *)0x73811D = 0x84;

    // Prevent it marking targets of remote players
    MemPutFast<BYTE>(0x742BF0, 0x8B);

    // Restore the mouse look state back to the default
    MemPutFast<bool>(0xB6EC2E, bMouseLookEnabled);

    // Restore the visual goggle mode back
    MemPutFast<bool>(0xC402B9, bInfraredVisionEnabled);
    MemPutFast<bool>(0xC402B8, bNightVisionEnabled);

    // Make players cough on fire extinguisher and teargas again
    MemPutFast<unsigned char>(0x4C03F0, 0x83);
    MemPutFast<unsigned char>(0x4C03F1, 0xF8);
    MemPutFast<unsigned char>(0x4C03F2, 0x29);
    MemPutFast<unsigned char>(0x4C03F8, 0x74);
    MemPutFast<unsigned char>(0x4C03F9, 0x09);
    MemPutFast<unsigned char>(0x4C03FA, 0x83);
    MemPutFast<unsigned char>(0x4C03FB, 0xF8);
    MemPutFast<unsigned char>(0x4C03FC, 0x2A);
    MemPutFast<unsigned char>(0x4C03FD, 0x74);
    MemPutFast<unsigned char>(0x4C03FE, 0x04);

    // make the CCamera::Using1stPersonWeaponMode function return true
    if (b1stPersonWeaponModeHackInPlace)
    {
        b1stPersonWeaponModeHackInPlace = false;

        MemPutFast<BYTE>(0x50BFF0, 0x66);
        MemPutFast<BYTE>(0x50BFF1, 0x8B);
        MemPutFast<BYTE>(0x50BFF2, 0x81);
    }

    if (bRadioHackInstalled)
    {
        // For tanks, to prevent our mouse movement affecting remote tanks
        // 006AEA25   0F85 60010000    JNZ gta_sa.006AEB8B
        // ^
        // 006AEA25   90               NOP
        // 006AEA26   E9 60010000      JMP gta_sa.006AEB8B
        MemPutFast<BYTE>(0x6AEA25, 0x0F);
        MemPutFast<BYTE>(0x6AEA26, 0x85);

        // Same for firetrucks and SWATs
        // 00729B96   0F85 75010000    JNZ gta_sa.00729D11
        // ^
        // 00729B96   90               NOP
        // 00729B97   E9 75010000      JMP gta_sa.00729D11
        MemPutFast<BYTE>(0x729B96, 0x0F);
        MemPutFast<BYTE>(0x729B97, 0x85);

        // Prevent the game making remote players vehicle's audio behave like locals (and deleting
        // radio etc when they are removed) - issue #95
        SetVehicleAudioContext(nullptr, NUM_RemoteVehicleAudioContext);

        bRadioHackInstalled = FALSE;
    }

    /*
    // ChrML: Force as high stats as we can go before screwing up. Players can't have different
    // stats or guns don't work. We can't have dual guns either due to some screwups.
    // Dual gun screwup: Sync code needs update and the gun pointing up needs to.
    localStatsData.StatTypesFloat [ 69 ] = 500.0f;
    localStatsData.StatTypesFloat [ 70 ] = 999.0f;
    localStatsData.StatTypesFloat [ 71 ] = 999.0f;
    localStatsData.StatTypesFloat [ 72 ] = 999.0f;
    localStatsData.StatTypesFloat [ 73 ] = 500.0f;
    localStatsData.StatTypesFloat [ 74 ] = 999.0f;
    localStatsData.StatTypesFloat [ 75 ] = 500.0f;
    localStatsData.StatTypesFloat [ 76 ] = 999.0f;
    localStatsData.StatTypesFloat [ 77 ] = 999.0f;
    localStatsData.StatTypesFloat [ 78 ] = 999.0f;
    localStatsData.StatTypesFloat [ 79 ] = 999.0f;
    */

    // ChrML: This causes the aiming issues
    // Restore the local player stats
    MemCpyFast((void*)0xb79380, &localStatsData.StatTypesFloat, sizeof(float) * MAX_FLOAT_STATS);
    MemCpyFast((void*)0xb79000, &localStatsData.StatTypesInt, sizeof(int) * MAX_INT_STATS);
    MemCpyFast((void*)0xb78f10, &localStatsData.StatReactionValue, sizeof(float) * MAX_REACTION_STATS);
}

VOID ReturnContextToLocalPlayer()
{
    if (bNotInLocalContext)
    {
        // Grab the remote data storage for the player we context switched to
        CPlayerPed* pContextSwitchedPlayerPed = dynamic_cast<CPlayerPed*>(pContextSwitchedPed);
        if (pContextSwitchedPlayerPed)
        {
            CRemoteDataStorageSA* data = CRemoteDataSA::GetRemoteDataStorage(pContextSwitchedPlayerPed);
            if (data)
            {
                // Store any changes the game has made to the pad
                CPad*            pLocalPad = pGameInterface->GetPad();
                CPadSAInterface* pLocalPadInterface = ((CPadSA*)pLocalPad)->GetInterface();
                MemCpyFast(&data->m_pad, pLocalPadInterface, sizeof(CPadSAInterface));
            }
        }

        pGameInterface->GetPad()->Restore();

        MemPutFast<float>(VAR_CameraRotation, fLocalPlayerCameraRotation);

        bNotInLocalContext = false;

        CPed*   pLocalPlayerPed = pGameInterface->GetPools()->GetPedFromRef((DWORD)1);  // the player
        CPedSA* pLocalPlayerPedSA = dynamic_cast<CPedSA*>(pLocalPlayerPed);
        if (pLocalPlayerPedSA)
        {
            CEntitySAInterface* ped = pLocalPlayerPedSA->GetInterface();
            MemPutFast<DWORD>(0xB7CD98, (DWORD)ped);
        }

        PostContextSwitch();
        pGameInterface->OnPedContextChange(NULL);

        if (m_pPostContextSwitchHandler)
        {
            m_pPostContextSwitchHandler();
        }
    }
    else
    {
        // Store any changes to the local-players stats?
        if (!bLocalStatsStatic)
        {
            assert(0);  // bLocalStatsStatic is always true
            MemCpyFast(&localStatsData.StatTypesFloat, (void*)0xb79380, sizeof(float) * MAX_FLOAT_STATS);
            MemCpyFast(&localStatsData.StatTypesInt, (void*)0xb79000, sizeof(int) * MAX_INT_STATS);
            MemCpyFast(&localStatsData.StatReactionValue, (void*)0xb78f10, sizeof(float) * MAX_REACTION_STATS);
        }
    }

    // radio change on startup hack
    // 0050237C   90               NOP
    MemSetFast((void*)0x50237C, 0x90, 5);
    MemSetFast((void*)0x5023A3, 0x90, 5);

    // We need to set this back, even if its the local player
    pGameInterface->SetGravity(fGlobalGravity);
}

void SwitchContext(CPed* thePed)
{
    pContextSwitchedPed = thePed;
    // Are we not already in another context?
    if (thePed && !bNotInLocalContext)
    {
        // Grab the local ped and the local pad
        CPed*            pLocalPlayerPed = pGameInterface->GetPools()->GetPedFromRef((DWORD)1);  // the player
        CPad*            pLocalPad = pGameInterface->GetPad();
        CPadSAInterface* pLocalPadInterface = ((CPadSA*)pLocalPad)->GetInterface();

        // We're not switching to local player
        if (thePed != pLocalPlayerPed)
        {
            // Store the local pad
            pLocalPad->Store();  // store a copy of the local pad internally

            // Grab the remote data storage for the player we're context switching to
            CPlayerPed* thePlayerPed = dynamic_cast<CPlayerPed*>(thePed);
            if (thePlayerPed)
            {
                CRemoteDataStorageSA* data = CRemoteDataSA::GetRemoteDataStorage(thePlayerPed);
                if (data)
                {
                    // We want the player to be seen as in targeting mode if they are right clicking and with weapons
                    CWeapon*          pWeapon = thePed->GetWeapon(thePed->GetCurrentWeaponSlot());
                    eWeaponType       currentWeapon = pWeapon->GetType();
                    CControllerState* cs = data->CurrentControllerState();
                    CWeaponStat*      pWeaponStat = NULL;
                    if (currentWeapon >= WEAPONTYPE_PISTOL && currentWeapon <= WEAPONTYPE_TEC9)
                    {
                        float fValue = data->m_stats.StatTypesFloat[pGameInterface->GetStats()->GetSkillStatIndex(currentWeapon)];
                        pWeaponStat = pGameInterface->GetWeaponStatManager()->GetWeaponStatsFromSkillLevel(currentWeapon, fValue);
                    }
                    else
                        pWeaponStat = pGameInterface->GetWeaponStatManager()->GetWeaponStats(currentWeapon);

                    if (cs->RightShoulder1 != 0 && (pWeaponStat && pWeaponStat->IsFlagSet(WEAPONTYPE_FIRSTPERSON)))
                    {
                        b1stPersonWeaponModeHackInPlace = true;

                        // make the CCamera::Using1stPersonWeaponMode function return true
                        MemPutFast<BYTE>(0x50BFF0, 0xB0);
                        MemPutFast<BYTE>(0x50BFF1, 0x01);
                        MemPutFast<BYTE>(0x50BFF2, 0xC3);
                    }

                    // Change the local player's pad to the remote player's
                    MemCpyFast(pLocalPadInterface, &data->m_pad, sizeof(CPadSAInterface));

                    // this is to fix the horn/siren
                    pLocalPad->SetHornHistoryValue((cs->ShockButtonL == 255));
                    // disables the impatient actions on remote players (which cause desync)
                    pLocalPad->SetLastTimeTouched(pGameInterface->GetSystemTime());

                    // this is to make movement work correctly
                    fLocalPlayerCameraRotation = *(float*)VAR_CameraRotation;
                    MemPutFast<float>(VAR_CameraRotation, data->m_fCameraRotation);

                    // Change the gravity to the remote player's
                    pGameInterface->SetGravity(data->m_fGravity);

                    // Disable mouselook for remote players (so the mouse doesn't affect them)
                    // Disable mouselook if they're not holding a 1st-person weapon
                    bool bDisableMouseLook = true;
                    if (pWeapon)
                    {
                        eWeaponType weaponType = pWeapon->GetType();
                        if (pWeaponStat->IsFlagSet(WEAPONTYPE_FIRSTPERSON))
                        {
                            bDisableMouseLook = false;
                        }
                    }

                    // Disable mouse look if they're not in a fight task and not aiming (strafing)
                    // Fix GitHub Issue #395
                    if (thePed->GetCurrentWeaponSlot() == eWeaponSlot::WEAPONSLOT_TYPE_UNARMED && data->m_pad.NewState.RightShoulder1 != 0 &&
                        thePed->GetPedIntelligence()->GetFightTask())
                        bDisableMouseLook = false;

                    // Disable mouse look if they're not underwater (Ped vertical rotation when diving)
                    // TODO - After merge PR #4401

                    bMouseLookEnabled = *(bool*)0xB6EC2E;
                    if (bDisableMouseLook)
                        *(bool*)0xB6EC2E = false;

                    // Disable the goggles
                    bInfraredVisionEnabled = *(bool*)0xC402B9;
                    MemPutFast<bool>(0xC402B9, false);
                    bNightVisionEnabled = *(bool*)0xC402B8;
                    MemPutFast<bool>(0xC402B8, false);

                    // Remove the code making players cough on fire extinguisher and teargas
                    MemSetFast((void*)0x4C03F0, 0x90, 3);
                    MemSetFast((void*)0x4C03F8, 0x90, 7);

                    // Prevent it calling ClearWeaponTarget for remote players
                    MemPutFast<BYTE>(0x609C80, 0xC3);

                    // Prevent rockets firing oddly
                    //*(BYTE *)0x73811C = 0x90;
                    //*(BYTE *)0x73811D = 0xE9;

                    // This is so weapon clicks and similar don't play for us when done remotely
                    MemPutFast<BYTE>(0x60F273, 0xEB);
                    MemPutFast<BYTE>(0x60F260, 0x90);
                    MemPutFast<BYTE>(0x60F261, 0x90);

                    // Prevent CCamera::SetNewPlayerWeaponMode being called
                    MemPutFast<BYTE>(0x50BFB0, 0xC2);
                    MemPutFast<BYTE>(0x50BFB1, 0x0C);
                    MemPutFast<BYTE>(0x50BFB2, 0x00);

                    // Prevent it calling CCamera::ClearPlayerWeaponMode for remote players
                    MemPutFast<BYTE>(0x50AB10, 0xC3);

                    // Prevent it marking targets of remote players
                    MemPutFast<BYTE>(0x742BF0, 0xC3);

                    // this is to prevent shooting players following the local camera
                    MemPutFast<BYTE>(0x687099, 0xEB);

                    // Prevent the game making remote player's weapons get switched by the local player's
                    MemPutFast<BYTE>(0x60D850, 0xC2);
                    MemPutFast<BYTE>(0x60D851, 0x04);
                    MemPutFast<BYTE>(0x60D852, 0x00);

                    // Change the local player's stats to the remote player's
                    if (data)
                    {
                        MemCpyFast((void*)0xb79380, data->m_stats.StatTypesFloat, sizeof(float) * MAX_FLOAT_STATS);
                        MemCpyFast((void*)0xb79000, data->m_stats.StatTypesInt, sizeof(int) * MAX_INT_STATS);
                        MemCpyFast((void*)0xb78f10, data->m_stats.StatReactionValue, sizeof(float) * MAX_REACTION_STATS);
                    }

                    /*
                    // ChrML: Force as high stats as we can go before screwing up. Players can't have different
                    //        stats or guns don't work. We can't have dual guns either due to some screwups.
                    //        Dual gun screwup: Sync code needs update and the gun pointing up needs to.
                    float* pfStats = (float*) 0xb79380;
                    pfStats [ 69 ] = 500.0f;
                    pfStats [ 70 ] = 999.0f;
                    pfStats [ 71 ] = 999.0f;
                    pfStats [ 72 ] = 999.0f;
                    pfStats [ 73 ] = 500.0f;
                    pfStats [ 74 ] = 999.0f;
                    pfStats [ 75 ] = 500.0f;
                    pfStats [ 76 ] = 999.0f;
                    pfStats [ 77 ] = 999.0f;
                    pfStats [ 78 ] = 999.0f;
                    pfStats [ 79 ] = 999.0f;
                    */

                    CPedSA* thePedSA = dynamic_cast<CPedSA*>(thePed);
                    if (thePedSA)
                    {
                        CEntitySAInterface* ped = thePedSA->GetInterface();
                        MemPutFast<DWORD>(0xB7CD98, (DWORD)ped);
                    }

                    // Remember that we're not in the local player's context any more (for switching back)
                    bNotInLocalContext = true;

                    // Call the pre-context switch handler we might have
                    if (m_pPreContextSwitchHandler)
                    {
                        CPlayerPed* pPlayerPed = dynamic_cast<CPlayerPed*>(thePed);
                        if (pPlayerPed)
                            m_pPreContextSwitchHandler(pPlayerPed);
                    }
                }
            }
        }
        else
        {
            // Set the local players gravity
            pGameInterface->SetGravity(fLocalPlayerGravity);

            if (bCustomCameraRotation)
                MemPutFast<float>(VAR_CameraRotation, fLocalPlayerCameraRotation);
        }
    }
    pGameInterface->OnPedContextChange(thePed);
}

void SwitchContext(CPedSAInterface* ped)
{
    SClientEntity<CPedSA>* pPedClientEntity = pGameInterface->GetPools()->GetPed((DWORD*)ped);
    CPed*                  thePed = pPedClientEntity ? pPedClientEntity->pEntity : nullptr;
    SwitchContext(thePed);
}

void SwitchContext(CVehicle* pVehicle)
{
    if (!pVehicle)
        return;

    // Grab the vehicle's internal interface
    CVehicleSA* pVehicleSA = dynamic_cast<CVehicleSA*>(pVehicle);

    DWORD dwVehicle = (DWORD)pVehicleSA->GetInterface();

    // Grab the driver of the vehicle
    CPed* thePed = pVehicle->GetDriver();
    if (thePed)
    {
        // Switch the context to the driver of the vehiclee
        SwitchContext(thePed);
        if (bNotInLocalContext)
        {
            // Prevent the game making remote players vehicle's audio behave like locals (and deleting
            // radio etc when they are removed) - issue #95
            SetVehicleAudioContext(pVehicleSA, NUM_LocalVehicleAudioContext);

            // For tanks, to prevent our mouse movement affecting remote tanks
            // 006AEA25   0F85 60010000    JNZ gta_sa.006AEB8B
            // V
            // 006AEA25   90               NOP
            // 006AEA26   E9 60010000      JMP gta_sa.006AEB8B
            MemPutFast<BYTE>(0x6AEA25, 0x90);
            MemPutFast<BYTE>(0x6AEA26, 0xE9);

            // Same for firetrucks and SWATs
            // 00729B96   0F85 75010000    JNZ gta_sa.00729D11
            // V
            // 00729B96   90               NOP
            // 00729B97   E9 75010000      JMP gta_sa.00729D11
            MemPutFast<BYTE>(0x729B96, 0x90);
            MemPutFast<BYTE>(0x729B97, 0xE9);

            bRadioHackInstalled = TRUE;
        }
        else
        {
            // 0050237C  |. E8 9F37FFFF    CALL gta_sa_u.004F5B20
            MemPutFast<BYTE>(0x50237C + 0, 0xE8);
            MemPutFast<BYTE>(0x50237C + 1, 0x9F);
            MemPutFast<BYTE>(0x50237C + 2, 0x37);
            MemPutFast<BYTE>(0x50237C + 3, 0xFF);
            MemPutFast<BYTE>(0x50237C + 4, 0xFF);

            // 0x5023A3
            MemPutFast<BYTE>(0x5023A3 + 0, 0xE8);
            MemPutFast<BYTE>(0x5023A3 + 1, 0xB8);
            MemPutFast<BYTE>(0x5023A3 + 2, 0x37);
            MemPutFast<BYTE>(0x5023A3 + 3, 0xFF);
            MemPutFast<BYTE>(0x5023A3 + 4, 0xFF);
        }
    }
}

void SwitchContext(CVehicleSAInterface* pVehicleInterface)
{
    // Grab the CVehicle for the given vehicle interface
    CPools*                    pPools = pGameInterface->GetPools();
    SClientEntity<CVehicleSA>* pVehicleClientEntity = pPools->GetVehicle((DWORD*)pVehicleInterface);
    CVehicle*                  pVehicle = pVehicleClientEntity ? pVehicleClientEntity->pEntity : nullptr;
    if (pVehicle)
    {
        SwitchContext(pVehicle);
    }
}

/************************** ACTUAL HOOK FUNCTIONS BELOW THIS LINE *******************************/

struct CSavedRegs
{
    DWORD eax, ecx, edx, ebx, esp, ebp, esi, edi;
};
static CSavedRegs PlayerPed__ProcessControl_Saved;

static void __declspec(naked) HOOK_CPlayerPed__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // Assumes no reentrancy
    // clang-format off
    __asm
    {
        mov     dwCurrentPlayerPed, ecx

        // Save incase of abort
        mov     PlayerPed__ProcessControl_Saved.eax, eax
        mov     PlayerPed__ProcessControl_Saved.ecx, ecx
        mov     PlayerPed__ProcessControl_Saved.edx, edx
        mov     PlayerPed__ProcessControl_Saved.ebx, ebx
        mov     PlayerPed__ProcessControl_Saved.esp, esp
        mov     PlayerPed__ProcessControl_Saved.ebp, ebp
        mov     PlayerPed__ProcessControl_Saved.esi, esi
        mov     PlayerPed__ProcessControl_Saved.edi, edi
        pushad
    }
    // clang-format on

    SwitchContext((CPedSAInterface*)dwCurrentPlayerPed);

    // clang-format off
    __asm
    {
        popad
        pushad
        call    BeginPlayerPedProcessControlTiming
        popad
        mov     edx, FUNC_CPlayerPed__ProcessControl
        call    edx
        pushad
        call    EndPlayerPedProcessControlTiming
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

static void __declspec(naked) CPlayerPed__ProcessControl_Abort()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        // restore stuff
        mov     eax, PlayerPed__ProcessControl_Saved.eax
        mov     ecx, PlayerPed__ProcessControl_Saved.ecx
        mov     edx, PlayerPed__ProcessControl_Saved.edx
        mov     ebx, PlayerPed__ProcessControl_Saved.ebx
        mov     esp, PlayerPed__ProcessControl_Saved.esp
        mov     ebp, PlayerPed__ProcessControl_Saved.ebp
        mov     esi, PlayerPed__ProcessControl_Saved.esi
        mov     edi, PlayerPed__ProcessControl_Saved.edi
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CAutomobile__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        pushad
        call    BeginAutomobileProcessControlTiming
        popad
        mov     edx, FUNC_CAutomobile__ProcessControl
        call    edx
        pushad
        call    EndAutomobileProcessControlTiming
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CMonsterTruck__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        mov     edx, FUNC_CMonsterTruck__ProcessControl
        call    edx
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CTrailer__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        mov     edx, FUNC_CTrailer__ProcessControl
        call    edx
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CQuadBike__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        mov     edx, FUNC_CQuadBike__ProcessControl
        call    edx
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CPlane__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        mov     edx, FUNC_CPlane__ProcessControl
        call    edx
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CBmx__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        mov     edx, FUNC_CBmx__ProcessControl
        call    edx
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CTrain__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        mov     edx, FUNC_CTrain__ProcessControl
        call    edx
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CBoat__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        mov     edx, FUNC_CBoat__ProcessControl
        call    edx
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CBike__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        mov     edx, FUNC_CBike__ProcessControl
        call    edx
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}

//--------------------------------------------------------------------------------------------

static void __declspec(naked) HOOK_CHeli__ProcessControl()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        mov     dwCurrentVehicle, ecx
        pushad
    }
    // clang-format on

    SwitchContext((CVehicleSAInterface*)dwCurrentVehicle);

    // clang-format off
    __asm
    {
        popad
        mov     edx, FUNC_CHeli__ProcessControl
        call    edx
        pushad
    }
    // clang-format on

    ReturnContextToLocalPlayer();

    // clang-format off
    __asm
    {
        popad
        retn
    }
    // clang-format on
}
