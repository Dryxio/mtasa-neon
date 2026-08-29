/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CGameSA.cpp
 *  PURPOSE:     Base game logic handling
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "../core/CModelCacheManager.h"
#define ALLOC_STATS_MODULE_NAME "game_sa"
#include "SharedUtil.hpp"
#include "SharedUtil.MemAccess.hpp"
#include <core/CCoreInterface.h>
#include "C3DMarkersSA.h"
#include "CAEAudioHardwareSA.h"
#include "CAERadioTrackManagerSA.h"
#include "CAESoundManagerSA.h"
#include "CAnimManagerSA.h"
#include "CAudioContainerSA.h"
#include "CCameraSA.h"
#include "CCarEnterExitSA.h"
#include "CCheckpointsSA.h"
#include "CClockSA.h"
#include "CColModelSA.h"
#include "CColStoreSA.h"
#include "CControllerConfigManagerSA.h"
#include "CCoronasSA.h"
#include "CEventListSA.h"
#include "CEntitySA.h"
#include "CExplosionManagerSA.h"
#include "CFileLoaderSA.h"
#include "CFireManagerSA.h"
#include "CFxSA.h"
#include "CFxSystemSA.h"
#include "CGameSA.h"
#include "CGaragesSA.h"
#include "CHandlingManagerSA.h"
#include "CHudSA.h"
#include "CKeyGenSA.h"
#include "CModelInfoSA.h"
#include "CObjectGroupPhysicalPropertiesSA.h"
#include "CNativeModelStoreSA.h"
#include "CNativeWorldPackSA.h"
#include "CPadSA.h"
#include "CPedSA.h"
#include "CPedModelInfoSA.h"
#include "CPickupsSA.h"
#include "CPlayerInfoSA.h"
#include "CPointLightsSA.h"
#include "CProjectileInfoSA.h"
#include "CRadarSA.h"
#include "CRopesSA.h"
#include "CSettingsSA.h"
#include "CStatsSA.h"
#include "CTaskManagementSystemSA.h"
#include "CTaskManagerSA.h"
#include "CTasksSA.h"
#include "TaskGoToSA.h"
#include "TaskBasicSA.h"
#include "CVisibilityPluginsSA.h"
#include "CWaterManagerSA.h"
#include "CWeaponInfoSA.h"
#include "CWeaponStatManagerSA.h"
#include "CWeatherSA.h"
#include "CWorldSA.h"
#include "D3DResourceSystemSA.h"
#include "CIplStoreSA.h"
#include "CBuildingRemovalSA.h"
#include "CCheckpointSA.h"
#include "CPtrNodeSingleLinkPoolSA.h"

extern CGameSA*        pGame;
extern CCoreInterface* g_pCore;

unsigned int& CGameSA::ClumpOffset = *(unsigned int*)0xB5F878;

unsigned int OBJECTDYNAMICINFO_MAX = *(uint32_t*)0x59FB4C != 0x90909090 ? *(uint32_t*)0x59FB4C : 160;  // default: 160

namespace
{
    constexpr std::uintptr_t BE_IN_COUPLE_FORWARD_EVENT_CALL = 0x684819;
    constexpr std::uintptr_t EVENT_GROUP_ADD = 0x4AB420;

    void* __fastcall HookBeInCoupleForwardEvent(void* eventGroup, void*, void* event, bool valid)
    {
        using GetEventType = int(__thiscall*)(void*);
        using AddEvent = void*(__thiscall*)(void*, void*, bool);

        auto**    eventVtable = event ? *reinterpret_cast<void***>(event) : nullptr;
        const int eventType = eventVtable ? reinterpret_cast<GetEventType>(eventVtable[1])(event) : -1;
        // BeInCouple ignores CEventGroup::Add's return value. The retail fact
        // being observed is this exact forwarding call, independently of the
        // partner decision maker retaining the cloned event.
        if (pGame)
            pGame->RecordAmbientPedCivilianCoupleForwardedEvent(eventGroup, eventType);
        return reinterpret_cast<AddEvent>(EVENT_GROUP_ADD)(eventGroup, event, valid);
    }

    bool IsExpectedBeInCoupleForwardEventCall()
    {
        const auto* call = reinterpret_cast<const unsigned char*>(BE_IN_COUPLE_FORWARD_EVENT_CALL);
        if (call[0] != 0xE8)
            return false;
        const auto relativeTarget = *reinterpret_cast<const std::int32_t*>(call + 1);
        return BE_IN_COUPLE_FORWARD_EVENT_CALL + 5 + relativeTarget == EVENT_GROUP_ADD;
    }

    void RemoveAmbientCoupleArmIK(CTaskManagerSA* taskManager)
    {
        if (!taskManager)
            return;

        CTask* ikTask = taskManager->GetTaskSecondary(TASK_SECONDARY_IK);
        if (!ikTask || ikTask->GetTaskType() != TASK_SIMPLE_IK_MANAGER)
            return;

        // Retail's BeInCouple abort only starts a blend-out. Once this ped
        // becomes a remote observer the secondary manager may stop processing,
        // leaving its two PointArm chains resident and blocking presentation.
        // Remove exactly the right/left arm children owned by BeInCouple while
        // the native owner lease still identifies this task tree.
        using RemoveIKChainTask = void(__thiscall*)(void*, int);
        auto* const managerInterface = ikTask->GetInterface();
        reinterpret_cast<RemoveIKChainTask>(0x633970)(managerInterface, 1);
        reinterpret_cast<RemoveIKChainTask>(0x633970)(managerInterface, 2);
    }

    constexpr const char* AMBIENT_COUPLE_PRESENTATION_ABI =
        "v2:Acquire(CPed*,CPed*,uint&);Update(uint,CPed*,CPed*);Release(uint,CPed*,CPed*);IsActive(uint,CPed*,CPed*)const;"
        "UpdateWithSides(uint,CPed*,CPed*,uchar,uchar)";

    void LogAmbientCouplePresentationAbiOnce()
    {
        static bool logged = false;
        if (logged || !g_pCore)
            return;

        logged = true;
        g_pCore->GetConsole()->Printf("[couple-presentation][abi] module=game_sa revision=2 signature=%s", AMBIENT_COUPLE_PRESENTATION_ABI);
    }

    void LogAmbientCouplePresentationAcquireReject(const char* reason, std::size_t nativeLeaseCount, std::size_t presentationLeaseCount,
                                                   unsigned int conflictLeaseId = 0, int conflictMember = -1)
    {
        static const char* lastReason = nullptr;
        if (!g_pCore || lastReason == reason)
            return;

        lastReason = reason;
        g_pCore->GetConsole()->Printf(
            "[couple-presentation][acquire-refused] module=game_sa reason=%s nativeLeases=%u presentationLeases=%u conflictLease=%u conflictMember=%d", reason,
            static_cast<unsigned int>(nativeLeaseCount), static_cast<unsigned int>(presentationLeaseCount), conflictLeaseId, conflictMember);
    }

    constexpr std::uintptr_t VEHICLE_RECORDING_PATHS = 0x97D880;
    constexpr std::uintptr_t VEHICLE_RECORDING_COUNT = 0x97F630;
    constexpr std::uintptr_t VEHICLE_PLAYBACK_ACTIVE = 0x97D6F0;
    constexpr std::size_t    VEHICLE_RECORDING_PATH_SIZE = 0x10;
    constexpr int            MAX_VEHICLE_RECORDINGS = 475;
    constexpr int            MAX_VEHICLE_PLAYBACKS = 16;

    constexpr std::uintptr_t FUNC_RequestVehicleRecording = 0x45A020;
    constexpr std::uintptr_t FUNC_RemoveVehicleRecording = 0x45A0A0;
    constexpr std::uintptr_t FUNC_IsVehicleRecordingLoaded = 0x45A060;
    constexpr std::uintptr_t FUNC_StartVehiclePlayback = 0x45A980;
    constexpr std::uintptr_t FUNC_StopVehiclePlayback = 0x45A280;
    constexpr std::uintptr_t FUNC_IsVehiclePlaybackActive = 0x4594C0;
    constexpr std::uintptr_t FUNC_SetVehiclePlaybackSpeed = 0x459660;

    constexpr std::uintptr_t GTA_PATH_FIND = 0x96F050;
    constexpr std::uintptr_t FUNC_StreamZoneModels = 0x40A560;
    constexpr std::uintptr_t FUNC_ClearPedModelSlots = 0x40BAA0;
    constexpr std::uintptr_t FUNC_FindZoneByLabel = 0x572C40;
    constexpr std::uintptr_t FUNC_SetModelIsDeletable = 0x409C10;
    constexpr std::uintptr_t FUNC_SetModelTxdIsDeletable = 0x409C70;
    constexpr std::uintptr_t FUNC_GeneratePedCreationCoors = 0x44E790;
    constexpr std::uintptr_t FUNC_GenerateCarCreationCoors2 = 0x424210;
    constexpr std::uintptr_t FUNC_PickARandomGroupOfOtherPeds = 0x610420;
    constexpr std::uintptr_t FUNC_PedIsAcceptableInCurrentZone = 0x610720;
    constexpr std::uintptr_t FUNC_TakePathWidthIntoAccount = 0x44DA30;
    constexpr std::uintptr_t FUNC_PedCreationDistMultiplier = 0x6116C0;
    constexpr std::uintptr_t FUNC_CullZonesFewerPeds = 0x72DD90;
    constexpr std::uintptr_t FUNC_ChooseGangOccupation = 0x611550;
    constexpr std::uintptr_t FUNC_ChooseCivilianOccupation = 0x612F90;
    constexpr std::uintptr_t FUNC_IsPositionClearForPed = 0x616860;
    constexpr std::uintptr_t GTA_CAMERA_GENERATION_DISTANCE_MULTIPLIER = 0xB6F11C;
    constexpr std::uintptr_t GTA_CAMERA_FORWARD_X = 0xB6F104;
    constexpr std::uintptr_t GTA_CAMERA_FORWARD_Y = 0xB6F108;
    constexpr std::uintptr_t GTA_PED_DENSITY_MULTIPLIER = 0x8D2530;
    constexpr std::uintptr_t GTA_MAX_PEDS_IN_USE = 0x8D2538;
    constexpr std::uintptr_t GTA_CURRENT_STREAMING_ZONE_TYPE = 0x8E4C20;
    constexpr std::uintptr_t GTA_LOADED_PED_MODELS = 0x8E4C00;

    struct SAmbientVehicleNodeAddressSA
    {
        unsigned short area;
        unsigned short node;
    };
    static_assert(sizeof(SAmbientVehicleNodeAddressSA) == 4, "Invalid vehicle path-node address size");

    bool GetAmbientVehiclePathNodePosition(const SAmbientVehicleNodeAddressSA& address, CVector& position, bool& waterPath)
    {
        constexpr unsigned int PATH_AREA_COUNT = 64;
        constexpr unsigned int PATH_NODE_ARRAY_OFFSET = 0x804;
        constexpr unsigned int PATH_VEHICLE_NODE_COUNT_OFFSET = 0x10C4;
        constexpr unsigned int PATH_NODE_SIZE = 0x1C;
        constexpr unsigned int PATH_NODE_POSITION_OFFSET = 0x08;
        constexpr unsigned int PATH_NODE_FLAGS_OFFSET = 0x18;

        if (address.area >= PATH_AREA_COUNT || address.node == 0xFFFF)
            return false;

        auto* const* nodeAreas = reinterpret_cast<unsigned char* const*>(GTA_PATH_FIND + PATH_NODE_ARRAY_OFFSET);
        const auto*  vehicleNodeCounts = reinterpret_cast<const unsigned int*>(GTA_PATH_FIND + PATH_VEHICLE_NODE_COUNT_OFFSET);
        const auto*  nodeArray = nodeAreas[address.area];
        if (!nodeArray || address.node >= vehicleNodeCounts[address.area])
            return false;

        const auto* node = nodeArray + address.node * PATH_NODE_SIZE;
        const auto* compressedPosition = reinterpret_cast<const short*>(node + PATH_NODE_POSITION_OFFSET);
        position = CVector(static_cast<float>(compressedPosition[0]) / 8.0f, static_cast<float>(compressedPosition[1]) / 8.0f,
                           static_cast<float>(compressedPosition[2]) / 8.0f);
        waterPath = (node[PATH_NODE_FLAGS_OFFSET] & 0x80) != 0;
        return true;
    }

    bool GetAmbientVehicleLaneOffset(const SAmbientVehicleNodeAddressSA& from, const SAmbientVehicleNodeAddressSA& to, unsigned int modelId,
                                     VehicleClass vehicleClass, float& offsetMeters)
    {
        constexpr unsigned int PATH_AREA_COUNT = 64;
        constexpr unsigned int PATH_NODE_ARRAY_OFFSET = 0x804;
        constexpr unsigned int PATH_CAR_LINK_ARRAY_OFFSET = 0x924;
        constexpr unsigned int PATH_NODE_LINK_ARRAY_OFFSET = 0xA44;
        constexpr unsigned int PATH_NAVI_LINK_ARRAY_OFFSET = 0xDA4;
        constexpr unsigned int PATH_VEHICLE_NODE_COUNT_OFFSET = 0x10C4;
        constexpr unsigned int PATH_CAR_LINK_COUNT_OFFSET = 0x1304;
        constexpr unsigned int PATH_ADDRESS_COUNT_OFFSET = 0x1424;
        constexpr unsigned int PATH_NODE_SIZE = 0x1C;
        constexpr unsigned int PATH_CAR_LINK_SIZE = 0x0E;

        if (from.area >= PATH_AREA_COUNT || to.area >= PATH_AREA_COUNT)
            return false;
        auto* const* pathNodes = reinterpret_cast<unsigned char* const*>(GTA_PATH_FIND + PATH_NODE_ARRAY_OFFSET);
        const auto*  vehicleNodeCounts = reinterpret_cast<const unsigned int*>(GTA_PATH_FIND + PATH_VEHICLE_NODE_COUNT_OFFSET);
        const auto*  addressCounts = reinterpret_cast<const unsigned int*>(GTA_PATH_FIND + PATH_ADDRESS_COUNT_OFFSET);
        if (!pathNodes[from.area] || from.node >= vehicleNodeCounts[from.area])
            return false;

        const unsigned char* node = pathNodes[from.area] + from.node * PATH_NODE_SIZE;
        const int            baseLink = *reinterpret_cast<const short*>(node + 0x10);
        const unsigned int   linkCount = node[0x18] & 0x0F;
        if (baseLink < 0 || linkCount == 0 || static_cast<unsigned int>(baseLink) + linkCount > addressCounts[from.area])
            return false;

        auto* const* nodeLinks = reinterpret_cast<SAmbientVehicleNodeAddressSA* const*>(GTA_PATH_FIND + PATH_NODE_LINK_ARRAY_OFFSET);
        auto* const* naviLinks = reinterpret_cast<unsigned short* const*>(GTA_PATH_FIND + PATH_NAVI_LINK_ARRAY_OFFSET);
        if (!nodeLinks[from.area] || !naviLinks[from.area])
            return false;

        unsigned short packedNaviLink = 0xFFFF;
        for (unsigned int index = 0; index < linkCount; ++index)
        {
            const auto& linked = nodeLinks[from.area][baseLink + index];
            if (linked.area == to.area && linked.node == to.node)
            {
                packedNaviLink = naviLinks[from.area][baseLink + index];
                break;
            }
        }
        if (packedNaviLink == 0xFFFF)
            return false;

        const unsigned int carLinkArea = packedNaviLink >> 10;
        const unsigned int carLinkId = packedNaviLink & 0x03FF;
        auto* const*       carLinks = reinterpret_cast<unsigned char* const*>(GTA_PATH_FIND + PATH_CAR_LINK_ARRAY_OFFSET);
        const auto*        carLinkCounts = reinterpret_cast<const unsigned int*>(GTA_PATH_FIND + PATH_CAR_LINK_COUNT_OFFSET);
        if (carLinkArea >= PATH_AREA_COUNT || !carLinks[carLinkArea] || carLinkId >= carLinkCounts[carLinkArea])
            return false;

        const unsigned char* carLink = carLinks[carLinkArea] + carLinkId * PATH_CAR_LINK_SIZE;
        const auto&          attachedTo = *reinterpret_cast<const SAmbientVehicleNodeAddressSA*>(carLink + 0x04);
        const bool           attachedToFrom = attachedTo.area == from.area && attachedTo.node == from.node;
        const bool           attachedToTo = attachedTo.area == to.area && attachedTo.node == to.node;
        if (!attachedToFrom && !attachedToTo)
            return false;
        const unsigned char laneFlags = carLink[0x0B];
        const unsigned int  lanesTowardAttached = laneFlags & 0x07;
        const unsigned int  lanesAwayFromAttached = (laneFlags >> 3) & 0x07;
        const unsigned int  laneCount = attachedToTo ? lanesTowardAttached : lanesAwayFromAttached;
        if (laneCount == 0 || ((modelId == 431 || modelId == 437) && laneCount < 2) || (vehicleClass == VehicleClass::BMX && laneCount >= 2))
            return false;

        const float oneWayOffset = lanesTowardAttached == 0      ? 0.5f - 0.5f * lanesAwayFromAttached
                                   : lanesAwayFromAttached == 0 ? 0.5f - 0.5f * lanesTowardAttached
                                                                 : static_cast<float>(carLink[0x0A]) * (1.0f / 86.4f) + 0.5f;
        offsetMeters = (oneWayOffset + rand() % laneCount) * 5.4f;
        if (vehicleClass == VehicleClass::BMX)
            offsetMeters += 1.458f;
        return std::isfinite(offsetMeters) && std::abs(offsetMeters) <= 50.0f;
    }
    constexpr std::uintptr_t GTA_NAVIGATION_ZONE_ARRAY = 0xBA3798;
    constexpr std::uintptr_t GTA_ZONE_INFO_ARRAY = 0xBA1DF0;
    constexpr std::uintptr_t GTA_CURRENT_POPCYCLE_ZONE = 0xC0BC64;
    constexpr std::uintptr_t GTA_CURRENT_POPCYCLE_ZONE_INFO = 0xC0BC68;
    constexpr std::uintptr_t GTA_CAR_GROUP_COUNTS = 0xC0EC78;
    constexpr std::uintptr_t GTA_CAR_GROUP_MODELS = 0xC0ED38;
    constexpr std::uintptr_t GTA_POPCYCLE_ZONE_TYPE = 0xC0BC6C;
    constexpr std::uintptr_t GTA_POPCYCLE_WEEKEND = 0xC0BC70;
    constexpr std::uintptr_t GTA_POPCYCLE_TIME_INDEX = 0xC0BC74;
    constexpr std::uintptr_t GTA_POPCYCLE_COP_PERCENTAGES = 0xC0E018;
    constexpr std::uintptr_t GTA_POPCYCLE_GANG_PERCENTAGES = 0xC0E1F8;
    constexpr std::uintptr_t GTA_POPCYCLE_DEALER_PERCENTAGES = 0xC0E3D8;
    constexpr std::uintptr_t GTA_POPCYCLE_MAX_PEDS = 0xC0E798;
    constexpr std::uintptr_t FUNC_GetCurrentPercOtherPeds = 0x610310;
    constexpr std::uintptr_t GTA_PED_GROUP_TRANSLATION = 0x8D2540;
    constexpr std::uintptr_t GTA_PED_GROUP_COUNTS = 0xC0ECC0;
    constexpr std::uintptr_t GTA_PED_GROUP_MODELS = 0xC0F358;
    constexpr std::uintptr_t GTA_DONT_CREATE_RANDOM_COPS = 0xC0FCB4;
    constexpr std::uintptr_t GTA_CURRENT_WORLD_ZONE = 0xC0FCBC;
    constexpr std::uintptr_t GTA_WEATHER_REGION = 0xC81314;
    constexpr std::uintptr_t GTA_CURRENT_LEVEL = 0xBA6718;
    constexpr std::uintptr_t FUNC_GangWarFightingGoingOn = 0x443AC0;
    constexpr unsigned int   POPCYCLE_GANG_GROUP_BASE = 18;
    constexpr unsigned int   POPCYCLE_TIME_COUNT = 12;
    constexpr unsigned int   POPCYCLE_WEEK_COUNT = 2;
    constexpr unsigned int   POPCYCLE_ZONE_COUNT = 20;
    constexpr int            POPCYCLE_COP_MIN_10_CASES[] = {4, 14, 16};
    constexpr int            POPCYCLE_COP_MIN_05_CASE = 5;
    constexpr int            POPCYCLE_COP_DISABLED_CASES[] = {8, 17};
    constexpr unsigned int   POPCYCLE_GROUP_COUNT = 33;
    constexpr unsigned int   POPCYCLE_PED_GROUP_COUNT = 57;
    constexpr unsigned int   POPCYCLE_PED_GROUP_CAPACITY = 21;
    constexpr unsigned int   AMBIENT_PED_GANG_COUNT = 10;
    constexpr unsigned int   AMBIENT_PED_GANG_MODELS_PER_GANG = 2;
    constexpr unsigned int   AMBIENT_PED_GANG_ROTATION_TICKS = 550;
    constexpr unsigned int   AMBIENT_PED_STOCK_MODEL_COUNT = 289;
    constexpr int            AMBIENT_PED_DEALER_MODELS[] = {28, 29, 30, 254};
    // CStreaming::ms_aDefaultCopModel at 0x8A5AA0 is indexed by
    // CTheZones::m_CurrLevel: countryside, LS, SF, then LV.
    constexpr int            AMBIENT_PED_COP_MODELS[] = {283, 280, 281, 282};
    constexpr unsigned int   STREAMING_GAME_REQUIRED = 0x2;
    constexpr unsigned int   STREAMING_KEEP_IN_MEMORY = 0x8;
    constexpr unsigned int   AMBIENT_PED_STREAMING_FLAG_MASK = STREAMING_GAME_REQUIRED | STREAMING_KEEP_IN_MEMORY;
    constexpr unsigned int   ZONE_TYPE_INFO = 2;
    constexpr unsigned int   PED_TYPE_CIVMALE = 4;
    constexpr unsigned int   PED_TYPE_CIVFEMALE = 5;
    constexpr unsigned int   PED_TYPE_COP = 6;
    constexpr unsigned int   PED_TYPE_GANG1 = 7;
    constexpr unsigned int   PED_TYPE_DEALER = 17;
    constexpr short          WEATHER_REGION_SF = 2;
    constexpr std::uintptr_t GTA_PED_GROUP_ACTIVE = 0xC098E0;
    constexpr std::uintptr_t GTA_PED_GROUPS = 0xC09920;
    constexpr std::uintptr_t FUNC_AddPedGroup = 0x5FB800;
    constexpr std::uintptr_t FUNC_RemovePedGroup = 0x5FB870;
    constexpr std::uintptr_t FUNC_GetPedsGroup = 0x5F7E80;
    constexpr std::uintptr_t FUNC_AddPedGroupMember = 0x5F6AE0;
    constexpr std::uintptr_t FUNC_ProcessPedGroupMembership = 0x5FBA60;
    constexpr std::uintptr_t FUNC_ProcessPedGroupIntelligence = 0x5FC4A0;
    constexpr std::uintptr_t FUNC_SetPedGroupDefaultTaskAllocatorType = 0x5FBB70;
    constexpr std::uintptr_t FUNC_FindAmbientGroupGroundZ = 0x5696C0;
    constexpr std::uintptr_t FUNC_GetRadianAngleBetweenPoints = 0x53CBE0;
    constexpr std::size_t    PED_GROUP_SIZE = 0x2D4;
    constexpr std::size_t    PED_GROUP_MEMBERSHIP_OFFSET = 0x08;
    constexpr std::size_t    PED_GROUP_INTELLIGENCE_OFFSET = 0x30;
    constexpr unsigned int   PED_GROUP_COUNT = 8;
    constexpr unsigned int   MAX_AMBIENT_PED_NATIVE_GROUPS = 5;
    constexpr int            PED_GROUP_LEADER_MEMBER_INDEX = 7;
    constexpr int            PED_GROUP_RANDOM_TASK_ALLOCATOR = 5;

    void* GetPedGroupInterface(unsigned int groupId)
    {
        return groupId < PED_GROUP_COUNT ? reinterpret_cast<void*>(GTA_PED_GROUPS + groupId * PED_GROUP_SIZE) : nullptr;
    }

    void* GetPedGroupMembershipInterface(unsigned int groupId)
    {
        auto* group = static_cast<unsigned char*>(GetPedGroupInterface(groupId));
        return group ? group + PED_GROUP_MEMBERSHIP_OFFSET : nullptr;
    }

    void* GetPedGroupIntelligenceInterface(unsigned int groupId)
    {
        auto* group = static_cast<unsigned char*>(GetPedGroupInterface(groupId));
        return group ? group + PED_GROUP_INTELLIGENCE_OFFSET : nullptr;
    }

    bool IsPedGroupSlotActive(unsigned int groupId)
    {
        return groupId < PED_GROUP_COUNT && reinterpret_cast<const bool*>(GTA_PED_GROUP_ACTIVE)[groupId];
    }

    int AcquireNonPlayerPedGroupSlot()
    {
        int groupId = reinterpret_cast<int(__cdecl*)()>(FUNC_AddPedGroup)();
        if (groupId != 0)
            return groupId;

        // CPedGroups slot 0 is GTA's player-group identity. If it happened to
        // be inactive, keep the slot allocated while asking GTA for the next
        // free slot, then release only the slot allocated by this call.
        groupId = reinterpret_cast<int(__cdecl*)()>(FUNC_AddPedGroup)();
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_RemovePedGroup)(0);
        return groupId;
    }

    int GetPedTaskType(CTaskSAInterface* task)
    {
        if (!task || !task->VTBL || !task->VTBL->GetTaskType)
            return -1;
        const auto vtableAddress = reinterpret_cast<std::uintptr_t>(task->VTBL);
        const auto getTaskTypeAddress = static_cast<std::uintptr_t>(task->VTBL->GetTaskType);
        if (vtableAddress < 0x400000 || vtableAddress >= 0x900000 || getTaskTypeAddress < 0x400000 || getTaskTypeAddress >= 0x900000 ||
            getTaskTypeAddress == 0x82263A)
        {
            return -1;
        }
        return reinterpret_cast<int(__thiscall*)(CTaskSAInterface*)>(task->VTBL->GetTaskType)(task);
    }

    void DestroyAmbientCouplePointArm(void*& task)
    {
        if (!task)
            return;
        auto* taskInterface = static_cast<CTaskSAInterface*>(task);
        reinterpret_cast<void(__thiscall*)(void*, unsigned int)>(taskInterface->VTBL->DeletingDestructor)(task, 1);
        task = nullptr;
    }

    bool UpdateAmbientCouplePointArm(void*& pointArmTask, CPedSAInterface* ped, int arm, const CVector& target)
    {
        if (!ped || arm < 0 || arm > 1)
            return false;

        using UpdatePointArmInfo = void(__thiscall*)(void*, const char*, CEntitySAInterface*, int, CVector, float, int);
        using GameNew = void*(__cdecl*)(std::size_t);
        using ConstructPointArm = void*(__thiscall*)(void*, const char*, int, CEntitySAInterface*, int, CVector, float, int);
        using ProcessPointArm = bool(__thiscall*)(void*, CPedSAInterface*);

        static const char purpose[] = "CoupleObserver";

        if (pointArmTask)
        {
            reinterpret_cast<UpdatePointArmInfo>(0x634370)(pointArmTask, purpose, nullptr, -1, target, 0.5f, 250);
        }
        else
        {
            pointArmTask = reinterpret_cast<GameNew>(0x61A5A0)(0x5C);
            if (!pointArmTask)
                return false;
            pointArmTask = reinterpret_cast<ConstructPointArm>(0x634150)(pointArmTask, purpose, arm, nullptr, -1, target, 0.5f, 250);
        }

        // A synced observer must not own a primary AI task, and its
        // CPlayerPed task manager rejects the retail secondary IK manager.
        // Keep only the retail point-arm presentation task in this local
        // resource lease and process it at the same per-frame boundary. Its
        // destructor still owns the native IK-chain cleanup.
        const auto processPed = reinterpret_cast<TaskSimpleVTBL*>(static_cast<CTaskSAInterface*>(pointArmTask)->VTBL)->ProcessPed;
        if (reinterpret_cast<ProcessPointArm>(processPed)(pointArmTask, ped))
        {
            DestroyAmbientCouplePointArm(pointArmTask);
            return false;
        }
        return true;
    }

    struct SAmbientPedPopulationZoneInfoSA
    {
        unsigned char gangStrength[AMBIENT_PED_GANG_COUNT];
        unsigned char dealerStrength;
        unsigned char color[4];
        unsigned char populationFlags;
        unsigned char raceFlags;
    };
    static_assert(sizeof(SAmbientPedPopulationZoneInfoSA) == 0x11, "Invalid CZoneInfo mirror size");

    struct SAmbientPedPopulationTargetsSA
    {
        float civilian{};
        float cop{};
        float gang{};
        float dealer{};
    };

    bool CalculateAmbientPedPopulationTargets(const SAmbientPedPopulationZoneInfoSA* zoneInfo, int timeIndex, int weekend, int zoneType,
                                              SAmbientPedPopulationTargetsSA& targets)
    {
        targets = {};
        if (!zoneInfo || timeIndex < 0 || timeIndex >= static_cast<int>(POPCYCLE_TIME_COUNT) || weekend < 0 ||
            weekend >= static_cast<int>(POPCYCLE_WEEK_COUNT) || zoneType < 0 || zoneType >= static_cast<int>(POPCYCLE_ZONE_COUNT))
        {
            return false;
        }

        // CPopulation::Update is deliberately cut short by multiplayer_sa, so
        // the four cached m_Num*Peds globals do not retain retail's dealer
        // share. Reproduce the read-only part of UpdatePercentages here rather
        // than calling it: the stock function also advances riot lighting.
        float gangStrength = 0.0f;
        for (const unsigned char strength : zoneInfo->gangStrength)
            gangStrength += strength;

        float dealerShare = std::max(0.1f, static_cast<float>(zoneInfo->dealerStrength) / 100.0f);
        float gangShare = std::min(0.5f, gangStrength / 100.0f);
        float copShare = gangShare >= 0.15f ? std::max(0.03f, 0.3f - gangShare) : std::max(0.02f, gangShare);
        // These are the literal PopType case values at 0x610881. Keep the
        // values, rather than the misleading reversed ped-group labels, tied
        // to the table index consumed by retail.
        if (std::find(std::begin(POPCYCLE_COP_MIN_10_CASES), std::end(POPCYCLE_COP_MIN_10_CASES), zoneType) != std::end(POPCYCLE_COP_MIN_10_CASES))
            copShare = std::max(0.1f, copShare);
        else if (zoneType == POPCYCLE_COP_MIN_05_CASE)
            copShare = std::max(0.05f, copShare);
        else if (std::find(std::begin(POPCYCLE_COP_DISABLED_CASES), std::end(POPCYCLE_COP_DISABLED_CASES), zoneType) != std::end(POPCYCLE_COP_DISABLED_CASES))
            copShare = 0.0f;

        float       civilianShare = 0.0f;
        const float allocatedShare = dealerShare + gangShare + copShare;
        if (allocatedShare <= 1.0f)
            civilianShare = 1.0f - allocatedShare;
        else
        {
            dealerShare /= allocatedShare;
            gangShare /= allocatedShare;
            copShare /= allocatedShare;
        }

        const std::size_t index = (static_cast<std::size_t>(timeIndex) * POPCYCLE_WEEK_COUNT + static_cast<std::size_t>(weekend)) * POPCYCLE_ZONE_COUNT +
                                  static_cast<std::size_t>(zoneType);
        const auto* maxPeds = reinterpret_cast<const unsigned char*>(GTA_POPCYCLE_MAX_PEDS);
        const auto* copPercentages = reinterpret_cast<const unsigned char*>(GTA_POPCYCLE_COP_PERCENTAGES);
        const auto* gangPercentages = reinterpret_cast<const unsigned char*>(GTA_POPCYCLE_GANG_PERCENTAGES);
        const auto* dealerPercentages = reinterpret_cast<const unsigned char*>(GTA_POPCYCLE_DEALER_PERCENTAGES);
        const float maximumPeds = static_cast<float>(maxPeds[index]);
        // The retail function returns its integer percentage in EAX. Treating it as
        // an x87 float reads stale floating-point state and intermittently corrupts
        // the ambient population target.
        const float otherPercentage = static_cast<float>(reinterpret_cast<int(__cdecl*)()>(FUNC_GetCurrentPercOtherPeds)()) / 100.0f;
        targets.civilian = maximumPeds * civilianShare * otherPercentage;
        targets.cop = maximumPeds * copShare * static_cast<float>(copPercentages[index]) / 100.0f;
        targets.gang = maximumPeds * gangShare * static_cast<float>(gangPercentages[index]) / 100.0f;
        targets.dealer = maximumPeds * dealerShare * static_cast<float>(dealerPercentages[index]) / 100.0f;
        return std::isfinite(targets.civilian) && std::isfinite(targets.cop) && std::isfinite(targets.gang) && std::isfinite(targets.dealer);
    }

    struct SAmbientPedNavigationZoneSA
    {
        char          infoLabel[8];
        char          textLabel[8];
        short         bounds[6];
        short         zoneInfoIndex;
        unsigned char type;
        unsigned char level;
    };
    static_assert(sizeof(SAmbientPedNavigationZoneSA) == 0x20, "Invalid CZone mirror size");

    struct SAmbientPedGroupTranslationSA
    {
        int pedGroupIds[3];
    };
    static_assert(sizeof(SAmbientPedGroupTranslationSA) == 0xC, "Invalid ped-group translation size");

#include "AmbientPedPopulationZonesSA.inc"

    constexpr std::uintptr_t GTA_TEXT = 0xC1B340;
    constexpr std::uintptr_t GTA_SETTINGS = 0xBA6748;
    constexpr std::uintptr_t FUNC_LoadMissionText = 0x69FBF0;
    constexpr std::uintptr_t FUNC_GetMissionText = 0x6A0050;
    constexpr std::uintptr_t FUNC_GetLoadedMissionText = 0x69FBD0;
    constexpr std::uintptr_t FUNC_AddMessageJump = 0x69F1E0;
    constexpr std::uintptr_t FUNC_AddBigMessage = 0x69F2B0;
    constexpr std::uintptr_t FUNC_AddBigMessageWithNumber = 0x69E5F0;
    constexpr std::uintptr_t FUNC_ClearThisPrint = 0x69EA30;
    constexpr std::uintptr_t FUNC_ClearThisBigPrint = 0x69EBE0;
    constexpr std::uintptr_t FUNC_SetHelpMessage = 0x588BE0;
    constexpr std::size_t    GTA_SUBTITLES_OFFSET = 0x44;
    constexpr unsigned int   GTA_BIG_MESSAGE_STYLE_COUNT = 7;

    constexpr std::uintptr_t FUNC_LoadFileCutscene = 0x4D5E80;
    constexpr std::uintptr_t FUNC_StartFileCutscene = 0x5B1460;
    constexpr std::uintptr_t FUNC_HasFileCutsceneFinished = 0x5B0570;
    constexpr std::uintptr_t FUNC_IsFileCutsceneSkipInputPressed = 0x4D5D10;
    constexpr std::uintptr_t FUNC_SkipFileCutscene = 0x5B1700;
    constexpr std::uintptr_t FUNC_DeleteFileCutscene = 0x4D5ED0;
    constexpr std::uintptr_t FUNC_FindCutsceneAudioTrack = 0x5AFA50;
    constexpr std::uintptr_t HOOKPOS_FileCutsceneSkipInput = 0x5B1947;
    constexpr std::uintptr_t VAR_FileCutsceneLoadStatus = 0xB5F84C;
    constexpr std::uintptr_t VAR_FileCutsceneRunning = 0xB5F851;
    constexpr std::uintptr_t VAR_FileCutsceneProcessing = 0xB5F852;
    constexpr std::uintptr_t VAR_FileCutsceneSkipped = 0xB5F854;
    constexpr std::uintptr_t VAR_FileCutsceneObjects = 0xBC3F18;
    constexpr std::uintptr_t VAR_FileCutsceneObjectCount = 0xBC3FE4;
    constexpr std::uintptr_t VAR_DisableStreaming = 0x9654B0;
    constexpr std::size_t    MAX_FILE_CUTSCENE_OBJECTS = 50;
    constexpr unsigned char  FILE_CUTSCENE_GLOBAL_AREA = 13;
    constexpr unsigned int   MODEL_CSPLAY = 1;
    constexpr unsigned int   MODEL_CUTOBJ01 = 300;
    constexpr unsigned int   MODEL_CUTOBJ13 = 312;
    constexpr std::size_t    MTA_CUTSCENE_OBJECT_SLOT_COUNT = MODEL_CUTOBJ13 - MODEL_CUTOBJ01 + 1;
    constexpr unsigned int   FILE_CUTSCENE_STREAMING_FLAGS = 0x1C;
    constexpr const char*    MTA_CUTSCENE_OBJECT_SPECIAL_CHARACTER_NAMES[] = {
        "RYDER2", "RYDER3", "EMMET", "ANDRE", "KENDL", "JETHRO", "ZERO", "TBONE", "SINDACO", "JANITOR", "BBTHIN", "SMOKEV", "PSYCHO",
    };
    static_assert(std::size(MTA_CUTSCENE_OBJECT_SPECIAL_CHARACTER_NAMES) == MTA_CUTSCENE_OBJECT_SLOT_COUNT);

    bool                       g_suppressManagedFileCutsceneSkipInput{};
    bool                       g_restoreManagedFileCutsceneModelMappings{};
    bool                       g_preloadingManagedFileCutscene{};
    char                       g_managedFileCutsceneName[8]{};
    CBaseModelInfoSAInterface* g_stockCutscenePlayerModelInfo{};
    CBaseModelInfoSAInterface* g_mtaSpecialCharacterModelInfo{};
    CStreamingInfo             g_stockCutscenePlayerStreamingInfo{};
    CStreamingInfo             g_mtaSpecialCharacterStreamingInfo{};
    bool                       g_cutscenePlayerMappingsCaptured{};
    CBaseModelInfoSAInterface* g_stockCutsceneObjectModelInfos[MTA_CUTSCENE_OBJECT_SLOT_COUNT]{};
    CBaseModelInfoSAInterface* g_mtaCutsceneObjectModelInfos[MTA_CUTSCENE_OBJECT_SLOT_COUNT]{};
    CStreamingInfo             g_stockCutsceneObjectStreamingInfos[MTA_CUTSCENE_OBJECT_SLOT_COUNT]{};
    bool                       g_cutsceneObjectMappingsCaptured{};
    bool                       g_managedFileCutsceneObjectAreasRestored{};
    unsigned int               g_managedFileCutsceneBlockingModel{UINT_MAX};
    unsigned int               g_managedFileCutsceneBlockingRefs{};

    bool RestoreManagedFileCutsceneObjectAreas()
    {
        if (g_managedFileCutsceneObjectAreasRestored)
            return true;

        const int objectCount = *reinterpret_cast<const int*>(VAR_FileCutsceneObjectCount);
        if (objectCount < 0 || static_cast<std::size_t>(objectCount) > MAX_FILE_CUTSCENE_OBJECTS)
            return false;

        auto* objects = reinterpret_cast<CEntitySAInterface**>(VAR_FileCutsceneObjects);
        for (int index = 0; index < objectCount; ++index)
        {
            if (!objects[index])
                return false;
        }

        // MTA patches CObject::Init so ordinary pickup objects are created in
        // area 0 instead of GTA's global area 13. CCutsceneObject inherits that
        // constructor, but file cutscenes rely on their actors and props being
        // visible across SET_AREA_VISIBLE transitions. Repair only the native
        // manager-owned objects after loading, leaving the pickup patch intact.
        for (int index = 0; index < objectCount; ++index)
            objects[index]->m_areaCode = FILE_CUTSCENE_GLOBAL_AREA;

        g_managedFileCutsceneObjectAreasRestored = true;
        return true;
    }

    bool ValidateManagedFileCutsceneModelMappings(CStreaming* streaming)
    {
        if (!streaming || !g_cutscenePlayerMappingsCaptured || !g_cutsceneObjectMappingsCaptured)
            return false;

        auto* currentModelInfo = CModelInfoSAInterface::GetModelInfo(MODEL_CSPLAY);
        auto* streamingInfo = streaming->GetStreamingInfo(MODEL_CSPLAY);
        if (!currentModelInfo || !streamingInfo || currentModelInfo != g_mtaSpecialCharacterModelInfo)
            return false;

        for (std::size_t index = 0; index < MTA_CUTSCENE_OBJECT_SLOT_COUNT; ++index)
        {
            const unsigned int modelId = MODEL_CUTOBJ01 + static_cast<unsigned int>(index);
            auto*              objectModelInfo = CModelInfoSAInterface::GetModelInfo(modelId);
            if (!objectModelInfo || !streaming->GetStreamingInfo(modelId) || objectModelInfo == g_stockCutsceneObjectModelInfos[index])
                return false;
        }
        return true;
    }

    int GetManagedFileCutsceneMtaReferences(unsigned int modelId)
    {
        CModelInfo* managedModel = pGame ? pGame->GetModelInfo(modelId, true) : nullptr;
        return managedModel ? managedModel->GetRefCount() : 0;
    }

    bool GetManagedFileCutsceneMappingBlocker(CStreaming* streaming, unsigned int& modelId, unsigned int& nativeReferences, int& mtaReferences)
    {
        if (!ValidateManagedFileCutsceneModelMappings(streaming))
            return false;

        auto* cutscenePlayerModel = CModelInfoSAInterface::GetModelInfo(MODEL_CSPLAY);
        nativeReferences = cutscenePlayerModel->usNumberOfRefs;
        mtaReferences = GetManagedFileCutsceneMtaReferences(MODEL_CSPLAY);
        if (nativeReferences != 0 || mtaReferences != 0)
        {
            modelId = MODEL_CSPLAY;
            return true;
        }

        for (std::size_t index = 0; index < MTA_CUTSCENE_OBJECT_SLOT_COUNT; ++index)
        {
            modelId = MODEL_CUTOBJ01 + static_cast<unsigned int>(index);
            auto* objectModelInfo = CModelInfoSAInterface::GetModelInfo(modelId);
            nativeReferences = objectModelInfo->usNumberOfRefs;
            mtaReferences = GetManagedFileCutsceneMtaReferences(modelId);
            if (nativeReferences != 0 || mtaReferences != 0)
                return true;
        }
        return false;
    }

    bool EvictManagedFileCutsceneModelCachePins()
    {
        CModelCacheManager* cacheManager = g_pCore ? g_pCore->GetModelCacheManager() : nullptr;
        if (!cacheManager)
            return false;

        // The core cache owns a revocable CModelInfoSA reference for nearby
        // ped models. A skin transition can leave that cache-only reference
        // resident forever after the last GTA ped released its RwObject. Ask
        // the cache to relinquish only its own known reference; Lua/resource
        // requests and live GTA instances remain visible as hard blockers.
        cacheManager->OnRestreamModel(MODEL_CSPLAY);
        for (unsigned int modelId = MODEL_CUTOBJ01; modelId <= MODEL_CUTOBJ13; ++modelId)
            cacheManager->OnRestreamModel(static_cast<unsigned short>(modelId));
        return true;
    }

    void ReportManagedFileCutsceneMappingWait(unsigned int modelId, unsigned int nativeReferences, int mtaReferences)
    {
        if (modelId == g_managedFileCutsceneBlockingModel && nativeReferences == g_managedFileCutsceneBlockingRefs)
            return;

        g_managedFileCutsceneBlockingModel = modelId;
        g_managedFileCutsceneBlockingRefs = nativeReferences;
        if (g_pCore && g_pCore->GetConsole())
        {
            g_pCore->GetConsole()->Printf("[native-file-cutscene][wait] name=%s model=%u nativeRefs=%u mtaRefs=%d", g_managedFileCutsceneName, modelId,
                                          nativeReferences, mtaReferences);
        }
    }

    void ReportManagedFileCutsceneMappingReady()
    {
        if (g_managedFileCutsceneBlockingModel != UINT_MAX && g_pCore && g_pCore->GetConsole())
        {
            g_pCore->GetConsole()->Printf("[native-file-cutscene][resume] name=%s releasedModel=%u", g_managedFileCutsceneName,
                                          g_managedFileCutsceneBlockingModel);
        }
        g_managedFileCutsceneBlockingModel = UINT_MAX;
        g_managedFileCutsceneBlockingRefs = 0;
    }

    bool InstallManagedFileCutsceneModelMappings(CStreaming* streaming)
    {
        if (!ValidateManagedFileCutsceneModelMappings(streaming))
            return false;

        unsigned int blockingModel{};
        unsigned int blockingNativeReferences{};
        int          blockingMtaReferences{};
        if (GetManagedFileCutsceneMappingBlocker(streaming, blockingModel, blockingNativeReferences, blockingMtaReferences))
            return false;

        auto* currentModelInfo = CModelInfoSAInterface::GetModelInfo(MODEL_CSPLAY);
        auto* streamingInfo = streaming->GetStreamingInfo(MODEL_CSPLAY);

        // MTA replaces GTA's stock slot 1 with TRUTH during initialization.
        // RequestSpecialModel cannot reverse that operation for CSPLAY because
        // CSPLAY is absent from GTA's extra-object directory; a failed retail
        // lookup leaves an undefined IMG range and hangs RetryLoadFile. Keep
        // the original model-info object and streaming metadata captured
        // before MTA's replacement, then request that ordinary stock model.
        streaming->RemoveModel(MODEL_CSPLAY);
        g_mtaSpecialCharacterModelInfo = currentModelInfo;
        g_mtaSpecialCharacterStreamingInfo = *streamingInfo;
        CModelInfoSAInterface::ms_modelInfoPtrs[MODEL_CSPLAY] = g_stockCutscenePlayerModelInfo;
        *streamingInfo = g_stockCutscenePlayerStreamingInfo;
        streaming->RequestModel(MODEL_CSPLAY, FILE_CUTSCENE_STREAMING_FLAGS);

        // MTA also repurposes CUTOBJ01 through CUTOBJ13 as playable special
        // characters. GTA assigns the first nonstandard cutscene model to
        // CUTOBJ01 before it scans for another free slot. An unskinned prop
        // such as SWEET2A's cigarette would consequently enter CPedModelInfo
        // and crash while that class builds its bone collision model. Restore
        // the original generic slots for the complete native load lifecycle.
        for (std::size_t index = 0; index < MTA_CUTSCENE_OBJECT_SLOT_COUNT; ++index)
        {
            const unsigned int modelId = MODEL_CUTOBJ01 + static_cast<unsigned int>(index);
            auto*              objectStreamingInfo = streaming->GetStreamingInfo(modelId);

            streaming->RemoveModel(modelId);
            g_mtaCutsceneObjectModelInfos[index] = CModelInfoSAInterface::GetModelInfo(modelId);
            CModelInfoSAInterface::ms_modelInfoPtrs[modelId] = g_stockCutsceneObjectModelInfos[index];
            *objectStreamingInfo = g_stockCutsceneObjectStreamingInfos[index];
        }
        g_managedFileCutsceneObjectAreasRestored = false;
        return true;
    }

    bool AreManagedFileCutsceneModelMappingsInstalled()
    {
        if (CModelInfoSAInterface::GetModelInfo(MODEL_CSPLAY) != g_stockCutscenePlayerModelInfo)
            return false;

        for (std::size_t index = 0; index < MTA_CUTSCENE_OBJECT_SLOT_COUNT; ++index)
        {
            if (CModelInfoSAInterface::GetModelInfo(MODEL_CUTOBJ01 + static_cast<unsigned int>(index)) != g_stockCutsceneObjectModelInfos[index])
                return false;
        }
        return true;
    }

    void RestoreManagedFileCutsceneModelMappings(CStreaming* streaming)
    {
        g_managedFileCutsceneObjectAreasRestored = false;
        if (!g_restoreManagedFileCutsceneModelMappings)
            return;

        if (g_cutsceneObjectMappingsCaptured)
        {
            for (std::size_t index = 0; index < MTA_CUTSCENE_OBJECT_SLOT_COUNT; ++index)
            {
                const unsigned int modelId = MODEL_CUTOBJ01 + static_cast<unsigned int>(index);
                auto*              streamingInfo = streaming->GetStreamingInfo(modelId);
                if (!streamingInfo || !g_mtaCutsceneObjectModelInfos[index])
                    continue;

                streaming->RemoveModel(modelId);
                // RemoveModel unloads the CUTOBJ clump but deliberately keeps
                // its CUTS.IMG directory range. Once the MTA ped model-info is
                // restored, RequestSpecialModel would mistake that stale range
                // for the already-resolved RYDER2..PSYCHO entry and request it
                // directly. An unskinned cutscene prop would then enter
                // CPedModelInfo and crash while building its bone collisions.
                // Clear the unlinked streaming record so GTA must resolve the
                // special character from extra.img again.
                *streamingInfo = CStreamingInfo{};
                CModelInfoSAInterface::ms_modelInfoPtrs[modelId] = g_mtaCutsceneObjectModelInfos[index];

                // RequestSpecialModel rebuilds the extra.img directory entry
                // and texture mapping for each MTA special character. Copying
                // the cutscene-era CStreamingInfo back verbatim can retain an
                // invalid archive range and makes GTA report model-load errors
                // such as KENDL in slot 304 after native cutscene teardown.
                streaming->RequestSpecialModel(modelId, MTA_CUTSCENE_OBJECT_SPECIAL_CHARACTER_NAMES[index], 0);
            }
        }

        auto* streamingInfo = streaming->GetStreamingInfo(MODEL_CSPLAY);
        if (g_cutscenePlayerMappingsCaptured && streamingInfo && g_mtaSpecialCharacterModelInfo)
        {
            // Native teardown has released every slot-1 cutscene object. Put
            // back MTA's exact TRUTH model-info object and CD metadata without
            // another special-directory lookup or a synchronous stream drain.
            streaming->RemoveModel(MODEL_CSPLAY);
            CModelInfoSAInterface::ms_modelInfoPtrs[MODEL_CSPLAY] = g_mtaSpecialCharacterModelInfo;
            *streamingInfo = g_mtaSpecialCharacterStreamingInfo;
            streaming->RequestModel(MODEL_CSPLAY, 0);
        }
        g_restoreManagedFileCutsceneModelMappings = false;
    }

    bool __cdecl FileCutsceneSkipInputHook()
    {
        // Managed multiplayer cutscenes route one participant's skip through
        // the server before asking every client to finish its local playback.
        // Unmanaged GTA cutscenes retain their original direct input path.
        return !g_suppressManagedFileCutsceneSkipInput && reinterpret_cast<bool(__cdecl*)()>(FUNC_IsFileCutsceneSkipInputPressed)();
    }

    bool IsKnownVehicleRecording(int recordingId)
    {
        const int count = *reinterpret_cast<const int*>(VEHICLE_RECORDING_COUNT);
        if (count <= 0 || count > MAX_VEHICLE_RECORDINGS)
            return false;

        for (int index = 0; index < count; ++index)
        {
            const auto path = VEHICLE_RECORDING_PATHS + index * VEHICLE_RECORDING_PATH_SIZE;
            if (*reinterpret_cast<const int*>(path) == recordingId)
                return true;
        }
        return false;
    }

    bool HasFreeVehiclePlaybackSlot()
    {
        const auto active = reinterpret_cast<const bool*>(VEHICLE_PLAYBACK_ACTIVE);
        for (int index = 0; index < MAX_VEHICLE_PLAYBACKS; ++index)
        {
            if (!active[index])
                return true;
        }
        return false;
    }

    const char* GetMissionText(const char* key)
    {
        if (!key || !key[0])
            return nullptr;

        return reinterpret_cast<const char*(__thiscall*)(void*, const char*)>(FUNC_GetMissionText)(reinterpret_cast<void*>(GTA_TEXT), key);
    }
}  // namespace

/**
 * \todo allow the addon to change the size of the pools (see 0x4C0270 - CPools::Initialise) (in start game?)
 */
CGameSA::CGameSA()
{
    try
    {
        pGame = this;

        // Find the game version and initialize m_eGameVersion so GetGameVersion() will return the correct value
        FindGameVersion();

        // Capture and preflight GTA's complete stock FileID layout before any
        // native-world feature can install patches. An unknown executable or
        // operand layout is refused before the later relocation commit.
        std::string fileIDError;
        if (!m_fileIDs.CaptureStockLayout(m_eGameVersion, fileIDError))
            throw std::runtime_error(SString("Unable to capture GTA FileID layout: %s", fileIDError.c_str()));

        // Native World cannot relocate GTA's populated inline model stores
        // after connection. Install only their content-neutral substrate on
        // every supported Neon launch; no server identity, cache path, model,
        // archive, or generation is known at this point.
        std::string nativeWorldFoundationError;
        if (!CNativeModelStoreSA::InstallProcessFoundation(m_eGameVersion, nativeWorldFoundationError))
        {
            SharedUtil::WriteDebugEvent(
                SString("[NativeWorldFoundation] state=unavailable detail=%s content=none stock-session=preserved", nativeWorldFoundationError.c_str()));
        }
        char        legacySelectorValue[8]{};
        const DWORD legacySelectorLength = GetEnvironmentVariableA("MTA_NATIVE_BW_MODEL_STORES", legacySelectorValue, sizeof(legacySelectorValue));
        const bool  legacySelectorEnabled = legacySelectorLength == 1 && legacySelectorValue[0] == '1';
        bool        suppressLegacyNativeWorld = false;
        const SNativeWorldStartupSelection startupSelection = g_pCore->BeginNativeWorldStartupSelection(legacySelectorEnabled);
        if (!startupSelection.diagnostic.empty())
            SharedUtil::WriteDebugEvent(SString("[NativeWorldAuthorization] %s", startupSelection.diagnostic.c_str()));
        if (!startupSelection.error.empty())
        {
            suppressLegacyNativeWorld = true;
            SharedUtil::WriteDebugEvent(
                SString("[NativeWorldAuthorization] state=startup-refused detail=%s activation=no lease=no", startupSelection.error.c_str()));
        }
        if (startupSelection.terminalRefusalRequired)
        {
            suppressLegacyNativeWorld = true;
            const SNativeWorldAuthorizationRecordResult result =
                g_pCore->FinishNativeWorldStartupSelection(startupSelection.ticketId, false, "selector-ambiguous");
            SharedUtil::WriteDebugEvent(SString("[NativeWorldAuthorization] %s", result.success ? result.diagnostic.c_str() : result.error.c_str()));
        }
        else if (startupSelection.ready)
        {
            suppressLegacyNativeWorld = true;
            CNativeWorldPackManagerSA::HandleStartupSelection(m_eGameVersion, startupSelection);
        }

        // The opt-in native extended-world foundation must relocate GTA's
        // inline model stores before CModelInfo::Initialise can populate them.
        if (!suppressLegacyNativeWorld)
            CNativeModelStoreSA::InstallFromEnvironment(m_eGameVersion);

        // Install the process-lifetime FileID tables only after the optional
        // model-store preflight has consumed its stock instruction signatures.
        // A few GTA instructions contain both operands, so this ordering keeps
        // both independently validated patches transactional.
        std::string fileIDRelocationError;
        if (!m_fileIDs.InstallStockRelocation(fileIDRelocationError))
            throw std::runtime_error(SString("Unable to relocate GTA FileIDs: %s", fileIDRelocationError.c_str()));
        CModelInfoSAInterface::ms_modelInfoPtrs = reinterpret_cast<CBaseModelInfoSAInterface**>(m_fileIDs.GetModelInfoArray());

        m_bAsyncScriptEnabled = false;
        m_bAsyncScriptForced = false;
        m_bASyncLoadingSuspended = false;
        m_iCheckStatus = 0;

        const unsigned int modelInfoMax = GetCountOfAllFileIDs();
        ModelInfo = new CModelInfoSA[modelInfoMax];
        ObjectGroupsInfo = new CObjectGroupPhysicalPropertiesSA[OBJECTDYNAMICINFO_MAX];

        SetInitialVirtualProtect();

        // CCutsceneMgr otherwise consumes local skip input inside Update and
        // finishes only this client's copy. Intercept that single call site so
        // a resource-owned cutscene can synchronize the decision at server.
        HookInstallCall(HOOKPOS_FileCutsceneSkipInput, reinterpret_cast<DWORD>(&FileCutsceneSkipInputHook));

        // Count only invocations of the single retail BeInCouple partner-
        // forwarding callsite. A global CEventGroup::Add hook would conflate
        // unrelated AI events and make the social propagation probe meaningless.
        const bool installBeInCoupleForwardHook = m_eGameVersion == VERSION_US_10 && IsExpectedBeInCoupleForwardEventCall();
        if (installBeInCoupleForwardHook)
            HookInstallCall(BE_IN_COUPLE_FORWARD_EVENT_CALL, reinterpret_cast<DWORD>(&HookBeInCoupleForwardEvent));

        // Set the model ids for all the CModelInfoSA instances
        for (unsigned int i = 0; i < modelInfoMax; i++)
        {
            ModelInfo[i].SetModelID(i);
        }

        // Prepare all object dynamic infos for CObjectGroupPhysicalPropertiesSA instances
        for (unsigned char i = 0; i < OBJECTDYNAMICINFO_MAX; i++)
        {
            ObjectGroupsInfo[i].SetGroup(i);
        }

        m_pAudioEngine = new CAudioEngineSA((CAudioEngineSAInterface*)CLASS_CAudioEngine);
        m_pAEAudioHardware = new CAEAudioHardwareSA((CAEAudioHardwareSAInterface*)CLASS_CAEAudioHardware);
        m_pAESoundManager = new CAESoundManagerSA((CAESoundManagerSAInterface*)CLASS_CAESoundManager);
        m_pAudioContainer = new CAudioContainerSA();
        m_pWorld = new CWorldSA();
        m_Pools = std::make_unique<CPoolsSA>();
        m_pClock = new CClockSA();
        m_pRadar = new CRadarSA();
        m_pCamera = new CCameraSA((CCameraSAInterface*)CLASS_CCamera);
        m_pCoronas = new CCoronasSA();
        m_pCheckpoints = new CCheckpointsSA();
        m_pPickups = new CPickupsSA();
        m_pExplosionManager = new CExplosionManagerSA();
        m_pHud = new CHudSA();
        m_pFireManager = new CFireManagerSA();
        m_p3DMarkers = new C3DMarkersSA();
        m_pPad = new CPadSA((CPadSAInterface*)CLASS_CPad);
        m_pCAERadioTrackManager = new CAERadioTrackManagerSA();
        m_pWeather = new CWeatherSA();
        m_pStats = new CStatsSA();
        m_pTaskManagementSystem = new CTaskManagementSystemSA();
        m_pSettings = new CSettingsSA();
        m_pCarEnterExit = new CCarEnterExitSA();
        m_pControllerConfigManager = new CControllerConfigManagerSA();
        m_pProjectileInfo = new CProjectileInfoSA();
        m_pRenderWare = new CRenderWareSA();
        m_HandlingManager = std::make_unique<CHandlingManagerSA>();
        m_pEventList = new CEventListSA();
        m_pGarages = new CGaragesSA((CGaragesSAInterface*)CLASS_CGarages);
        m_pTasks = new CTasksSA((CTaskManagementSystemSA*)m_pTaskManagementSystem);
        m_pAnimManager = new CAnimManagerSA;
        m_pStreaming = new CStreamingSA;
        if (suppressLegacyNativeWorld)
            CNativeWorldPackManagerSA::AttachAuthorizedStreaming(static_cast<CStreamingSA*>(m_pStreaming));
        else
            CNativeWorldPackManagerSA::InstallFromEnvironment(static_cast<CStreamingSA*>(m_pStreaming));
        m_pVisibilityPlugins = new CVisibilityPluginsSA;
        m_pKeyGen = new CKeyGenSA;
        m_pRopes = new CRopesSA;
        m_pFx = new CFxSA((CFxSAInterface*)CLASS_CFx);
        m_pFxManager = new CFxManagerSA((CFxManagerSAInterface*)CLASS_CFxManager);
        m_pWaterManager = new CWaterManagerSA();
        m_pWeaponStatsManager = new CWeaponStatManagerSA();
        m_pPointLights = new CPointLightsSA();
        m_collisionStore = new CColStoreSA();
        m_pIplStore = new CIplStoreSA();
        m_pCoverManager = new CCoverManagerSA();
        m_pPlantManager = new CPlantManagerSA();
        m_pBuildingRemoval = new CBuildingRemovalSA();
        m_pVehicleAudioSettingsManager = std::make_unique<CVehicleAudioSettingsManagerSA>();

        m_pRenderer = std::make_unique<CRendererSA>();

        // Normal weapon types (WEAPONSKILL_STD)
        for (int i = 0; i < NUM_WeaponInfosStdSkill; i++)
        {
            eWeaponType weaponType = (eWeaponType)(WEAPONTYPE_PISTOL + i);
            WeaponInfos[i] = new CWeaponInfoSA((CWeaponInfoSAInterface*)(ARRAY_WeaponInfo + i * CLASSSIZE_WeaponInfo), weaponType);
            m_pWeaponStatsManager->CreateWeaponStat(WeaponInfos[i], (eWeaponType)(weaponType - WEAPONTYPE_PISTOL), WEAPONSKILL_STD);
        }

        // Extra weapon types for skills (WEAPONSKILL_POOR,WEAPONSKILL_PRO,WEAPONSKILL_SPECIAL)
        int          index;
        eWeaponSkill weaponSkill = eWeaponSkill::WEAPONSKILL_POOR;
        for (int skill = 0; skill < 3; skill++)
        {
            // STD is created first, then it creates "extra weapon types" (poor, pro, special?) but in the enum 1 = STD which meant the STD weapon skill
            // contained pro info
            if (skill >= 1)
            {
                if (skill == 1)
                {
                    weaponSkill = eWeaponSkill::WEAPONSKILL_PRO;
                }
                if (skill == 2)
                {
                    weaponSkill = eWeaponSkill::WEAPONSKILL_SPECIAL;
                }
            }
            for (int i = 0; i < NUM_WeaponInfosOtherSkill; i++)
            {
                eWeaponType weaponType = (eWeaponType)(WEAPONTYPE_PISTOL + i);
                index = NUM_WeaponInfosStdSkill + skill * NUM_WeaponInfosOtherSkill + i;
                WeaponInfos[index] = new CWeaponInfoSA((CWeaponInfoSAInterface*)(ARRAY_WeaponInfo + index * CLASSSIZE_WeaponInfo), weaponType);
                m_pWeaponStatsManager->CreateWeaponStat(WeaponInfos[index], weaponType, weaponSkill);
            }
        }

        m_pPlayerInfo = new CPlayerInfoSA((CPlayerInfoSAInterface*)CLASS_CPlayerInfo);

        // Init cheat name => address map
        m_Cheats[CHEAT_HOVERINGCARS] = new SCheatSA((BYTE*)VAR_HoveringCarsEnabled);
        m_Cheats[CHEAT_FLYINGCARS] = new SCheatSA((BYTE*)VAR_FlyingCarsEnabled);
        m_Cheats[CHEAT_EXTRABUNNYHOP] = new SCheatSA((BYTE*)VAR_ExtraBunnyhopEnabled);
        m_Cheats[CHEAT_EXTRAJUMP] = new SCheatSA((BYTE*)VAR_ExtraJumpEnabled);

        // New cheats for Anticheat
        m_Cheats[CHEAT_TANKMODE] = new SCheatSA((BYTE*)VAR_TankModeEnabled, false);
        m_Cheats[CHEAT_NORELOAD] = new SCheatSA((BYTE*)VAR_NoReloadEnabled, false);
        m_Cheats[CHEAT_PERFECTHANDLING] = new SCheatSA((BYTE*)VAR_PerfectHandling, false);
        m_Cheats[CHEAT_ALLCARSHAVENITRO] = new SCheatSA((BYTE*)VAR_AllCarsHaveNitro, false);
        m_Cheats[CHEAT_BOATSCANFLY] = new SCheatSA((BYTE*)VAR_BoatsCanFly, false);
        m_Cheats[CHEAT_INFINITEOXYGEN] = new SCheatSA((BYTE*)VAR_InfiniteOxygen, false);
        m_Cheats[CHEAT_WALKUNDERWATER] = new SCheatSA((BYTE*)VAR_WalkUnderwater, false);
        m_Cheats[CHEAT_FASTERCLOCK] = new SCheatSA((BYTE*)VAR_FasterClock, false);
        m_Cheats[CHEAT_FASTERGAMEPLAY] = new SCheatSA((BYTE*)VAR_FasterGameplay, false);
        m_Cheats[CHEAT_SLOWERGAMEPLAY] = new SCheatSA((BYTE*)VAR_SlowerGameplay, false);
        m_Cheats[CHEAT_ALWAYSMIDNIGHT] = new SCheatSA((BYTE*)VAR_AlwaysMidnight, false);
        m_Cheats[CHEAT_FULLWEAPONAIMING] = new SCheatSA((BYTE*)VAR_FullWeaponAiming, false);
        m_Cheats[CHEAT_INFINITEHEALTH] = new SCheatSA((BYTE*)VAR_InfiniteHealth, false);
        m_Cheats[CHEAT_NEVERWANTED] = new SCheatSA((BYTE*)VAR_NeverWanted, false);
        m_Cheats[CHEAT_HEALTARMORMONEY] = new SCheatSA((BYTE*)VAR_HealthArmorMoney, false);

        // Change pool sizes here
        // Native IPLs allocate buildings while GTA is still loading the static
        // world. Install the final capacity before CPools::Initialise instead
        // of relying on MTA's later destructive runtime-resize path.
        m_Pools->SetPoolCapacity(BUILDING_POOL, MAX_BUILDINGS);  // Default is 13000
        m_Pools->SetPoolCapacity(TASK_POOL, 5000);               // Default is 500
        m_Pools->SetPoolCapacity(OBJECT_POOL, MAX_OBJECTS);      // Default is 350
        m_Pools->SetPoolCapacity(EVENT_POOL, 5000);              // Default is 200
        // The native store transaction installed this before GTA allocated the
        // pools. A late write would silently desynchronise the pool from the
        // validated startup layout.
        dassert(m_Pools->GetPoolCapacity(COL_MODEL_POOL) == MAX_COL_MODELS);
        m_Pools->SetPoolCapacity(ENV_MAP_MATERIAL_POOL, 16000);                        // Default is 4096
        m_Pools->SetPoolCapacity(ENV_MAP_ATOMIC_POOL, 4000);                           // Default is 1024
        m_Pools->SetPoolCapacity(SPEC_MAP_MATERIAL_POOL, 16000);                       // Default is 4096
        m_Pools->SetPoolCapacity(ENTRY_INFO_NODE_POOL, MAX_ENTRY_INFO_NODES);          // Default is 500
        m_Pools->SetPoolCapacity(POINTER_SINGLE_LINK_POOL, MAX_POINTER_SINGLE_LINKS);  // Default is 70000
        m_Pools->SetPoolCapacity(POINTER_DOUBLE_LINK_POOL, MAX_POINTER_DOUBLE_LINKS);  // Default is 3200
        dassert(m_Pools->GetPoolCapacity(POINTER_SINGLE_LINK_POOL) == MAX_POINTER_SINGLE_LINKS);

        // GTA passes the list allocation size through two 32-bit immediates. Keep
        // enough RwObject links for distant LOD preloading without changing when
        // the streamer creates or removes an instance.
        MemPut<DWORD>(0x05B8E55, MAX_RWOBJECT_INSTANCES * 12);  // Default is 1000 * 12
        MemPut<DWORD>(0x05B8EB0, MAX_RWOBJECT_INSTANCES * 12);  // Default is 1000 * 12

        // Increase matrix array size
        MemPut<int>(0x054F3A1, MAX_OBJECTS * 3);  // Default is 900

        CEntitySAInterface::StaticSetHooks();
        CPhysicalSAInterface::StaticSetHooks();
        CObjectSA::StaticSetHooks();
        CModelInfoSA::StaticSetHooks();
        CPlayerPedSA::StaticSetHooks();
        CRenderWareSA::StaticSetHooks();
        CRenderWareSA::StaticSetClothesReplacingHooks();
        CTasksSA::StaticSetHooks();
        CPedSA::StaticSetHooks();
        CSettingsSA::StaticSetHooks();
        CFxSystemSA::StaticSetHooks();
        CFileLoaderSA::StaticSetHooks();
        D3DResourceSystemSA::StaticSetHooks();
        CVehicleSA::StaticSetHooks();
        CCheckpointSA::StaticSetHooks();
        CHudSA::StaticSetHooks();
        CFireSA::StaticSetHooks();
        CPtrNodeSingleLinkPoolSA::StaticSetHooks();
        CVehicleAudioSettingsManagerSA::StaticSetHooks();
        CPointLightsSA::StaticSetHooks();
    }
    catch (const std::bad_alloc& e)
    {
        std::string error = _("Failed initialization game_sa");
        error += "\n";
        error += _("Memory allocations failed");
        error += ": ";
        error += e.what();

        MessageBoxUTF8(nullptr, error, _("Error"), MB_ICONERROR | MB_OK);
        ExitProcess(EXIT_FAILURE);
    }
    catch (const std::exception& e)
    {
        std::string error = _("Failed initialization game_sa");
        error += "\n";
        error += _("Information");
        error += ": ";
        error += e.what();

        MessageBoxUTF8(nullptr, error, _("Error"), MB_ICONERROR | MB_OK);
        ExitProcess(EXIT_FAILURE);
    }
}

CGameSA::~CGameSA()
{
    ResetAmbientPedPopulationModels();
    delete reinterpret_cast<CPlayerInfoSA*>(m_pPlayerInfo);

    for (int i = 0; i < NUM_WeaponInfosTotal; i++)
    {
        delete reinterpret_cast<CWeaponInfoSA*>(WeaponInfos[i]);
    }

    delete reinterpret_cast<CFxSA*>(m_pFx);
    delete reinterpret_cast<CRopesSA*>(m_pRopes);
    delete reinterpret_cast<CKeyGenSA*>(m_pKeyGen);
    delete reinterpret_cast<CVisibilityPluginsSA*>(m_pVisibilityPlugins);
    delete reinterpret_cast<CStreamingSA*>(m_pStreaming);
    delete reinterpret_cast<CAnimManagerSA*>(m_pAnimManager);
    delete reinterpret_cast<CTasksSA*>(m_pTasks);
    delete reinterpret_cast<CTaskManagementSystemSA*>(m_pTaskManagementSystem);
    delete reinterpret_cast<CStatsSA*>(m_pStats);
    delete reinterpret_cast<CWeatherSA*>(m_pWeather);
    delete reinterpret_cast<CAERadioTrackManagerSA*>(m_pCAERadioTrackManager);
    delete reinterpret_cast<CPadSA*>(m_pPad);
    delete reinterpret_cast<C3DMarkersSA*>(m_p3DMarkers);
    delete reinterpret_cast<CFireManagerSA*>(m_pFireManager);
    delete reinterpret_cast<CHudSA*>(m_pHud);
    delete reinterpret_cast<CExplosionManagerSA*>(m_pExplosionManager);
    delete reinterpret_cast<CPickupsSA*>(m_pPickups);
    delete reinterpret_cast<CCheckpointsSA*>(m_pCheckpoints);
    delete reinterpret_cast<CCoronasSA*>(m_pCoronas);
    delete reinterpret_cast<CCameraSA*>(m_pCamera);
    delete reinterpret_cast<CRadarSA*>(m_pRadar);
    delete reinterpret_cast<CClockSA*>(m_pClock);
    delete reinterpret_cast<CWorldSA*>(m_pWorld);
    delete reinterpret_cast<CAudioEngineSA*>(m_pAudioEngine);
    delete reinterpret_cast<CAEAudioHardwareSA*>(m_pAEAudioHardware);
    delete reinterpret_cast<CAudioContainerSA*>(m_pAudioContainer);
    delete reinterpret_cast<CPointLightsSA*>(m_pPointLights);
    delete static_cast<CColStoreSA*>(m_collisionStore);
    delete static_cast<CIplStore*>(m_pIplStore);
    delete static_cast<CBuildingRemovalSA*>(m_pBuildingRemoval);
    delete m_pCoverManager;
    delete m_pPlantManager;

    delete[] ModelInfo;
    delete[] ObjectGroupsInfo;
}

CWeaponInfo* CGameSA::GetWeaponInfo(eWeaponType weapon, eWeaponSkill skill)
{
    if ((skill == WEAPONSKILL_STD && weapon >= WEAPONTYPE_UNARMED && weapon < WEAPONTYPE_LAST_WEAPONTYPE) ||
        (skill != WEAPONSKILL_STD && weapon >= WEAPONTYPE_PISTOL && weapon <= WEAPONTYPE_TEC9))
    {
        int offset = 0;
        switch (skill)
        {
            case WEAPONSKILL_STD:
                offset = 0;
                break;
            case WEAPONSKILL_POOR:
                offset = NUM_WeaponInfosStdSkill - WEAPONTYPE_PISTOL;
                break;
            case WEAPONSKILL_PRO:
                offset = NUM_WeaponInfosStdSkill + NUM_WeaponInfosOtherSkill - WEAPONTYPE_PISTOL;
                break;
            case WEAPONSKILL_SPECIAL:
                offset = NUM_WeaponInfosStdSkill + 2 * NUM_WeaponInfosOtherSkill - WEAPONTYPE_PISTOL;
                break;
            default:
                break;
        }
        return WeaponInfos[offset + weapon];
    }
    else
        return NULL;
}

void CGameSA::Pause(bool bPaused)
{
    MemPutFast<bool>(0xB7CB49, bPaused);  // CTimer::m_UserPause
}

CModelInfo* CGameSA::GetModelInfo(DWORD dwModelID, bool bCanBeInvalid)
{
    if (dwModelID < GetCountOfAllFileIDs())
    {
        if (ModelInfo[dwModelID].IsValid() || bCanBeInvalid)
        {
            return &ModelInfo[dwModelID];
        }
        return nullptr;
    }
    return nullptr;
}

/**
 * Starts the game
 * \todo make addresses into constants
 */
void CGameSA::StartGame()
{
    if (!VerifyNativeWorldStartupBeforeStartGame())
        return;
    SetSystemState(SystemState::GS_INIT_PLAYING_GAME);
    MemPutFast<BYTE>(0xB7CB49, 0);  // CTimer::m_UserPause
    MemPutFast<BYTE>(0xBA67A4, 0);  // FrontEndMenuManager + 0x5C
}

bool CGameSA::VerifyNativeWorldStartupBeforeStartGame()
{
    return CNativeWorldPackManagerSA::VerifyAuthorizedStartupBeforeStartGame();
}

void CGameSA::CancelNativeWorldStartupActivation()
{
    CNativeWorldPackManagerSA::CancelAuthorizedActivation();
}

/**
 * Sets the part of the game loading process the game is in.
 * @param dwState DWORD containing a valid state 0 - 9
 */
void CGameSA::SetSystemState(SystemState State)
{
    MemPutFast<DWORD>(0xC8D4C0, (DWORD)State);  // gGameState
}

SystemState CGameSA::GetSystemState()
{
    return *(SystemState*)0xC8D4C0;  // gGameState
}

/**
 * This adds the local player to the ped pool, nothing else
 * @return BOOL TRUE if success, FALSE otherwise
 */
bool CGameSA::InitLocalPlayer(CClientPed* pClientPed)
{
    CPoolsSA* pools = (CPoolsSA*)GetPools();
    if (pools)
    {
        //* HACKED IN HERE FOR NOW *//
        CPedSAInterface* pInterface = pools->GetPedInterface((DWORD)1);

        if (pInterface)
        {
            pools->ResetPedPoolCount();
            pools->AddPed(pClientPed, (DWORD*)pInterface);
            return TRUE;
        }

        return false;
    }
    return true;
}

float CGameSA::GetGravity()
{
    return *(float*)(0x863984);
}

void CGameSA::SetGravity(float fGravity)
{
    MemPut<float>(0x863984, fGravity);
}

float CGameSA::GetGameSpeed()
{
    return *(float*)(0xB7CB64);
}

void CGameSA::SetGameSpeed(float fSpeed)
{
    MemPutFast<float>(0xB7CB64, fSpeed);
}

void CGameSA::Reset()
{
    ResetAmbientPedPopulationModels();
    CNativeWorldPackManagerSA::LogLifecycleTelemetry("CGameSA::Reset-begin");

    // Things to do if the game was loaded
    if (GetSystemState() == SystemState::GS_PLAYING_GAME)
    {
        // Extinguish all fires
        m_pFireManager->ExtinguishAllFires();

        // Restore camera stuff
        m_pCamera->Restore();
        m_pCamera->SetFadeColor(0, 0, 0);
        m_pCamera->Fade(0, FADE_OUT);

        Pause(false);  // We don't have to pause as the fadeout will stop the sound. Pausing it will make the fadein next start ugly
        m_pHud->Disable(false);

        // Restore the HUD
        m_pHud->Disable(false);
        m_pHud->SetComponentVisible(HUD_ALL, true);

        // Restore model dummies' positions
        CModelInfoSA::ResetAllVehicleDummies();
        CModelInfoSA::RestoreAllObjectsPropertiesGroups();
        // restore default properties of all CObjectGroupPhysicalPropertiesSA instances
        CObjectGroupPhysicalPropertiesSA::RestoreDefaultValues();

        // Restore vehicle model wheel sizes
        CModelInfoSA::ResetAllVehiclesWheelSizes();

        // Restore changed TXD IDs
        CModelInfoSA::StaticResetTextureDictionaries();

        // Restore default world state
        RestoreGameWorld();

        // Reset building pool to default capacity if a server enlarged it
        if (m_Pools->GetBuildingsPool().GetSize() != MAX_BUILDINGS)
            SetBuildingPoolSize(MAX_BUILDINGS);
    }
}

void CGameSA::Terminate()
{
    // Initiate the destruction
    delete this;

// Dump any memory leaks if DETECT_LEAK is defined
#ifdef DETECT_LEAKS
    DumpUnfreed();
#endif
}

void CGameSA::Initialize()
{
    CNativeModelStoreSA::LogDiagnostics("CGameSA::Initialize");
    CNativeWorldPackManagerSA::LogLifecycleTelemetry("CGameSA::Initialize");

    // Initialize garages
    m_pGarages->Initialize();
    SetupSpecialCharacters();
    SetupBrokenModels();
    m_pRenderWare->Initialize();

    // *Sebas* Hide the GTA:SA Main menu.
    MemPutFast<BYTE>(CLASS_CMenuManager + 0x5C, 0);
}

eGameVersion CGameSA::GetGameVersion()
{
    return m_eGameVersion;
}

SNativeWorldTransportPublishResult CGameSA::PublishNativeWorldTransportOffer(const SNativeWorldTransportOffer& offer)
{
    return CNativeWorldPackManagerSA::PublishTransportOffer(offer);
}

bool CGameSA::IsNativeWorldModelIdReserved(uint32_t modelId) const
{
    return CNativeWorldPackManagerSA::IsModelIdReserved(modelId);
}

void CGameSA::PrepareNativeWorldStreaming(const CVector& position)
{
    CNativeWorldPackManagerSA::PrepareStreamingAtPosition(position);
}

bool CGameSA::BeginNativeWorldDrain()
{
    return CNativeWorldPackManagerSA::BeginRuntimeDrain();
}

bool CGameSA::IsNativeWorldDrainQuiescent() const
{
    return CNativeWorldPackManagerSA::IsRuntimeDrainQuiescent();
}

bool CGameSA::TeardownNativeWorldContent()
{
    return CNativeWorldPackManagerSA::TeardownRuntimeContent();
}

bool CGameSA::IsNativeWorldContentDetached() const
{
    return CNativeWorldPackManagerSA::IsRuntimeContentDetached();
}

ENativeWorldRuntimeAdmissionReadiness CGameSA::GetNativeWorldRuntimeAdmissionReadiness() const
{
    return CNativeWorldPackManagerSA::GetRuntimeAdmissionReadiness();
}

bool CGameSA::ActivateNativeWorldRuntimeSelection(const SNativeWorldStartupSelection& selection, std::string& error)
{
    return CNativeWorldPackManagerSA::HandleRuntimeSelection(m_eGameVersion, selection, error);
}

bool CGameSA::ReleaseDetachedNativeWorldSession(const SNativeWorldStartupSelection& expectedSelection, std::string& error)
{
    return CNativeWorldPackManagerSA::ReleaseDetachedRuntimeSession(expectedSelection, error);
}

void CGameSA::UpdateAmbientPedPopulationModels(const CVector& origin)
{
    if (!m_areAmbientPedPopulationModelsActive)
    {
        InitializeAmbientPedPopulationZones();
        InitializeAmbientPedPopulationStreamingLease();
        std::fill(&m_ambientPedGangModels[0][0], &m_ambientPedGangModels[0][0] + AMBIENT_PED_GANG_COUNT * AMBIENT_PED_GANG_MODELS_PER_GANG, -1);
        m_ambientPedDealerModel = -1;
        m_ambientPedCopModel = -1;
        m_ambientPedGangModelUpdateCounter = 0;
        m_ambientPedGangModelRotation = 0;
    }
    m_ambientPedPopulationOriginZ = origin.fZ;

    // MTA skips GTA's ambient StreamZoneModels caller so unmanaged population
    // cannot appear. Calling the intact ped-only pass preserves the stock zone
    // model cadence without enabling CPopulation::AddToPopulation.
    const auto* loadedPedModels = reinterpret_cast<const int*>(GTA_LOADED_PED_MODELS);
    for (unsigned int slot = 0; slot < 8; ++slot)
    {
        const int modelId = loadedPedModels[slot];
        if (modelId >= 0 && modelId < static_cast<int>(AMBIENT_PED_STOCK_MODEL_COUNT))
            m_ambientPedPopulationStreamingTouched[modelId] = true;
    }

    ProtectAmbientPedPopulationStreamingRequests();
    m_areAmbientPedPopulationModelsActive = true;
    reinterpret_cast<void(__cdecl*)(const CVector&)>(FUNC_StreamZoneModels)(origin);

    // Stock StreamZoneModels leaves its eight slots pinned with
    // KEEP_IN_MEMORY, while ClearSlots only releases GAME_REQUIRED. Convert
    // only newly acquired pins into a reversible Game SA lease and preserve
    // flags that another subsystem already owned before this update.
    for (unsigned int slot = 0; slot < 8; ++slot)
    {
        const int modelId = loadedPedModels[slot];
        if (modelId < 0 || modelId >= static_cast<int>(AMBIENT_PED_STOCK_MODEL_COUNT))
            continue;

        m_ambientPedPopulationStreamingTouched[modelId] = true;
        if ((m_ambientPedPopulationModelFlagSnapshot[modelId] & STREAMING_KEEP_IN_MEMORY) == 0)
        {
            if (auto* streamingInfo = m_pStreaming->GetStreamingInfo(modelId))
                streamingInfo->flg = static_cast<unsigned char>((streamingInfo->flg & ~STREAMING_KEEP_IN_MEMORY) | STREAMING_GAME_REQUIRED);
        }

        const auto* modelInfo = CModelInfoSAInterface::GetModelInfo(modelId);
        if (modelInfo && (m_ambientPedPopulationTxdFlagSnapshot[modelId] & STREAMING_KEEP_IN_MEMORY) == 0)
        {
            const auto txdModelId = GetBaseIDforTXD() + modelInfo->usTextureDictionary;
            if (auto* streamingInfo = m_pStreaming->GetStreamingInfo(txdModelId))
                streamingInfo->flg = static_cast<unsigned char>((streamingInfo->flg & ~STREAMING_KEEP_IN_MEMORY) | STREAMING_GAME_REQUIRED);
        }
    }
    ProtectAmbientPedPopulationStreamingRequests();
    UpdateAmbientPedCopModel();
    UpdateAmbientPedDealerModel();
    UpdateAmbientPedGangModels();
    PreserveAmbientPedPopulationStreamingFlags();
}

void CGameSA::ResetAmbientPedPopulationModels()
{
    if (!m_areAmbientPedPopulationModelsActive)
    {
        RestoreAmbientPedPopulationZones();
        return;
    }

    ProtectAmbientPedPopulationStreamingRequests();
    ResetAmbientPedCopModel();
    ResetAmbientPedDealerModel();
    ResetAmbientPedGangModels();

    // These eight stock slots are otherwise orphaned when MTA's ambient caller
    // remains disabled. Their KEEP_IN_MEMORY pins were converted above so the
    // native routine can release our GAME_REQUIRED lease here.
    reinterpret_cast<void(__cdecl*)(int)>(FUNC_ClearPedModelSlots)(8);
    RestoreAmbientPedPopulationStreamingLease();
    *reinterpret_cast<int*>(GTA_CURRENT_STREAMING_ZONE_TYPE) = -1;
    RestoreAmbientPedPopulationZones();
    m_areAmbientPedPopulationModelsActive = false;
}

void CGameSA::InitializeAmbientPedPopulationStreamingLease()
{
    std::fill(std::begin(m_ambientPedPopulationStreamingTouched), std::end(m_ambientPedPopulationStreamingTouched), false);
    for (unsigned int modelId = 0; modelId < AMBIENT_PED_STOCK_MODEL_COUNT; ++modelId)
    {
        const auto* streamingInfo = m_pStreaming->GetStreamingInfo(modelId);
        m_ambientPedPopulationModelFlagSnapshot[modelId] = streamingInfo ? static_cast<unsigned char>(streamingInfo->flg & AMBIENT_PED_STREAMING_FLAG_MASK) : 0;

        m_ambientPedPopulationTxdFlagSnapshot[modelId] = 0;
        const auto* modelInfo = CModelInfoSAInterface::GetModelInfo(modelId);
        if (modelInfo)
        {
            const auto txdModelId = GetBaseIDforTXD() + modelInfo->usTextureDictionary;
            if (const auto* txdStreamingInfo = m_pStreaming->GetStreamingInfo(txdModelId))
                m_ambientPedPopulationTxdFlagSnapshot[modelId] = txdStreamingInfo->flg & AMBIENT_PED_STREAMING_FLAG_MASK;
        }
    }
}

void CGameSA::PreserveAmbientPedPopulationStreamingFlags()
{
    for (unsigned int modelId = 0; modelId < AMBIENT_PED_STOCK_MODEL_COUNT; ++modelId)
    {
        if (!m_ambientPedPopulationStreamingTouched[modelId])
            continue;
        if (auto* streamingInfo = m_pStreaming->GetStreamingInfo(modelId))
        {
            streamingInfo->flg =
                static_cast<unsigned char>((streamingInfo->flg & ~STREAMING_KEEP_IN_MEMORY) | m_ambientPedPopulationModelFlagSnapshot[modelId]);
        }

        const auto* modelInfo = CModelInfoSAInterface::GetModelInfo(modelId);
        if (modelInfo)
        {
            const auto txdModelId = GetBaseIDforTXD() + modelInfo->usTextureDictionary;
            if (auto* streamingInfo = m_pStreaming->GetStreamingInfo(txdModelId))
            {
                streamingInfo->flg =
                    static_cast<unsigned char>((streamingInfo->flg & ~STREAMING_KEEP_IN_MEMORY) | m_ambientPedPopulationTxdFlagSnapshot[modelId]);
            }
        }
    }
}

void CGameSA::ProtectAmbientPedPopulationStreamingRequests()
{
    for (unsigned int modelId = 0; modelId < AMBIENT_PED_STOCK_MODEL_COUNT; ++modelId)
    {
        if (!m_ambientPedPopulationStreamingTouched[modelId])
            continue;

        const auto modelSnapshot = m_ambientPedPopulationModelFlagSnapshot[modelId];
        if ((modelSnapshot & STREAMING_GAME_REQUIRED) != 0 && (modelSnapshot & STREAMING_KEEP_IN_MEMORY) == 0)
        {
            if (auto* streamingInfo = m_pStreaming->GetStreamingInfo(modelId))
                streamingInfo->flg |= STREAMING_KEEP_IN_MEMORY;
        }

        const auto txdSnapshot = m_ambientPedPopulationTxdFlagSnapshot[modelId];
        if ((txdSnapshot & STREAMING_GAME_REQUIRED) != 0 && (txdSnapshot & STREAMING_KEEP_IN_MEMORY) == 0)
        {
            const auto* modelInfo = CModelInfoSAInterface::GetModelInfo(modelId);
            if (modelInfo)
            {
                const auto txdModelId = GetBaseIDforTXD() + modelInfo->usTextureDictionary;
                if (auto* streamingInfo = m_pStreaming->GetStreamingInfo(txdModelId))
                    streamingInfo->flg |= STREAMING_KEEP_IN_MEMORY;
            }
        }
    }
}

void CGameSA::RestoreAmbientPedPopulationStreamingLease()
{
    for (unsigned int modelId = 0; modelId < AMBIENT_PED_STOCK_MODEL_COUNT; ++modelId)
    {
        if (!m_ambientPedPopulationStreamingTouched[modelId])
            continue;

        if (auto* streamingInfo = m_pStreaming->GetStreamingInfo(modelId))
        {
            streamingInfo->flg =
                static_cast<unsigned char>((streamingInfo->flg & ~AMBIENT_PED_STREAMING_FLAG_MASK) | m_ambientPedPopulationModelFlagSnapshot[modelId]);
        }

        const auto* modelInfo = CModelInfoSAInterface::GetModelInfo(modelId);
        if (modelInfo)
        {
            const auto txdModelId = GetBaseIDforTXD() + modelInfo->usTextureDictionary;
            if (auto* streamingInfo = m_pStreaming->GetStreamingInfo(txdModelId))
            {
                streamingInfo->flg =
                    static_cast<unsigned char>((streamingInfo->flg & ~AMBIENT_PED_STREAMING_FLAG_MASK) | m_ambientPedPopulationTxdFlagSnapshot[modelId]);
            }
        }
        m_ambientPedPopulationStreamingTouched[modelId] = false;
    }
}

void CGameSA::InitializeAmbientPedPopulationZones()
{
    if (m_areAmbientPedPopulationZonesInitialized)
        return;

    auto* const zoneInfos = reinterpret_cast<SAmbientPedPopulationZoneInfoSA*>(GTA_ZONE_INFO_ARRAY);
    std::memcpy(m_ambientPedPopulationZoneSnapshot, zoneInfos, sizeof(m_ambientPedPopulationZoneSnapshot));

    ApplyAmbientPedPopulationZoneBootstrap();
    std::memcpy(m_ambientPedPopulationZoneExpected, zoneInfos, sizeof(m_ambientPedPopulationZoneExpected));
    m_areAmbientPedPopulationZonesInitialized = true;
}

void CGameSA::ApplyAmbientPedPopulationZoneBootstrap()
{
    auto* const zoneInfos = reinterpret_cast<SAmbientPedPopulationZoneInfoSA*>(GTA_ZONE_INFO_ARRAY);

    // MTA suppresses main.scm, including the one stock bootstrap that assigns
    // population families and gang territory to GTA's INFO zones. Reapply only
    // the fields owned by that immutable block: resetting the whole array here
    // would erase radar colours and live zone settings from other resources.
    for (const auto& profile : g_ambientPedPopulationZoneProfiles)
    {
        const short zoneIndex = reinterpret_cast<short(__cdecl*)(const char*, int)>(FUNC_FindZoneByLabel)(profile.label, ZONE_TYPE_INFO);
        if (zoneIndex < 0 || zoneIndex >= 380)
            continue;

        const auto* navigationZones = reinterpret_cast<const SAmbientPedNavigationZoneSA*>(GTA_NAVIGATION_ZONE_ARRAY);
        const short zoneInfoIndex = navigationZones[zoneIndex].zoneInfoIndex;
        if (zoneInfoIndex < 0 || zoneInfoIndex >= 380)
            continue;

        auto& zoneInfo = zoneInfos[zoneInfoIndex];
        std::copy(std::begin(profile.gangStrength), std::end(profile.gangStrength), std::begin(zoneInfo.gangStrength));
        zoneInfo.dealerStrength = profile.dealerStrength;
        zoneInfo.populationFlags =
            static_cast<unsigned char>((zoneInfo.populationFlags & 0x60) | (profile.populationType & 0x1F) | (profile.noCops ? 0x80 : 0));
        zoneInfo.raceFlags = static_cast<unsigned char>((zoneInfo.raceFlags & 0xF0) | (profile.races & 0x0F));
    }
}

bool CGameSA::ResetAmbientPedPopulationZonesToBootstrap()
{
    InitializeAmbientPedPopulationZones();

    const auto* zoneInfos = reinterpret_cast<const SAmbientPedPopulationZoneInfoSA*>(GTA_ZONE_INFO_ARRAY);
    const auto* navigationZones = reinterpret_cast<const SAmbientPedNavigationZoneSA*>(GTA_NAVIGATION_ZONE_ARRAY);
    for (const auto& profile : g_ambientPedPopulationZoneProfiles)
    {
        const short zoneIndex = reinterpret_cast<short(__cdecl*)(const char*, int)>(FUNC_FindZoneByLabel)(profile.label, ZONE_TYPE_INFO);
        if (zoneIndex < 0 || zoneIndex >= 380)
            return false;

        const short zoneInfoIndex = navigationZones[zoneIndex].zoneInfoIndex;
        if (zoneInfoIndex < 0 || zoneInfoIndex >= 380)
            return false;

        const auto& current = zoneInfos[zoneInfoIndex];
        const auto& expected = *reinterpret_cast<const SAmbientPedPopulationZoneInfoSA*>(m_ambientPedPopulationZoneExpected[zoneInfoIndex]);
        if (!std::equal(std::begin(current.gangStrength), std::end(current.gangStrength), std::begin(expected.gangStrength)) ||
            current.dealerStrength != expected.dealerStrength || (current.populationFlags & 0x9F) != (expected.populationFlags & 0x9F) ||
            (current.raceFlags & 0x0F) != (expected.raceFlags & 0x0F))
        {
            return false;
        }
    }

    ApplyAmbientPedPopulationZoneBootstrap();
    std::memcpy(m_ambientPedPopulationZoneExpected, zoneInfos, sizeof(m_ambientPedPopulationZoneExpected));
    *reinterpret_cast<int*>(GTA_CURRENT_STREAMING_ZONE_TYPE) = -1;
    return true;
}

bool CGameSA::SetAmbientPedPopulationZoneState(const char* label, const SAmbientPedPopulationZoneState& state)
{
    if (!label || !*label)
        return false;

    const auto labelLength = std::strlen(label);
    if (labelLength > 7)
        return false;
    for (std::size_t index = 0; index < labelLength; ++index)
    {
        const auto character = static_cast<unsigned char>(label[index]);
        if (!(character >= 'A' && character <= 'Z') && !(character >= '0' && character <= '9'))
            return false;
    }
    const auto profile = std::find_if(std::begin(g_ambientPedPopulationZoneProfiles), std::end(g_ambientPedPopulationZoneProfiles),
                                      [label](const SAmbientPedPopulationZoneProfileSA& candidate) { return std::strcmp(candidate.label, label) == 0; });
    if (profile == std::end(g_ambientPedPopulationZoneProfiles))
        return false;

    constexpr unsigned int POPULATION_ZONE_FIELD_MASK =
        static_cast<unsigned int>(EAmbientPedPopulationZoneField::PopulationType) | static_cast<unsigned int>(EAmbientPedPopulationZoneField::Races) |
        static_cast<unsigned int>(EAmbientPedPopulationZoneField::DealerStrength) | static_cast<unsigned int>(EAmbientPedPopulationZoneField::NoCops);
    if ((state.fields & ~POPULATION_ZONE_FIELD_MASK) != 0 || state.populationType >= 20 || state.races > 0x0F || state.noCops > 1 ||
        (state.gangMask & ~0x03FFu) != 0)
        return false;

    InitializeAmbientPedPopulationZones();
    const short zoneIndex = reinterpret_cast<short(__cdecl*)(const char*, int)>(FUNC_FindZoneByLabel)(label, ZONE_TYPE_INFO);
    if (zoneIndex < 0 || zoneIndex >= 380)
        return false;

    const auto* navigationZones = reinterpret_cast<const SAmbientPedNavigationZoneSA*>(GTA_NAVIGATION_ZONE_ARRAY);
    const short zoneInfoIndex = navigationZones[zoneIndex].zoneInfoIndex;
    if (zoneInfoIndex < 0 || zoneInfoIndex >= 380)
        return false;

    auto* const zoneInfos = reinterpret_cast<SAmbientPedPopulationZoneInfoSA*>(GTA_ZONE_INFO_ARRAY);
    auto&       zoneInfo = zoneInfos[zoneInfoIndex];
    auto&       expected = *reinterpret_cast<SAmbientPedPopulationZoneInfoSA*>(m_ambientPedPopulationZoneExpected[zoneInfoIndex]);
    const auto  hasField = [&state](EAmbientPedPopulationZoneField field) { return (state.fields & static_cast<unsigned int>(field)) != 0; };

    if ((hasField(EAmbientPedPopulationZoneField::PopulationType) && (zoneInfo.populationFlags & 0x1F) != (expected.populationFlags & 0x1F)) ||
        (hasField(EAmbientPedPopulationZoneField::Races) && (zoneInfo.raceFlags & 0x0F) != (expected.raceFlags & 0x0F)) ||
        (hasField(EAmbientPedPopulationZoneField::DealerStrength) && zoneInfo.dealerStrength != expected.dealerStrength) ||
        (hasField(EAmbientPedPopulationZoneField::NoCops) && (zoneInfo.populationFlags & 0x80) != (expected.populationFlags & 0x80)))
    {
        return false;
    }
    for (unsigned int gangId = 0; gangId < AMBIENT_PED_GANG_COUNT; ++gangId)
    {
        if ((state.gangMask & (1u << gangId)) != 0 && zoneInfo.gangStrength[gangId] != expected.gangStrength[gangId])
            return false;
    }

    if (hasField(EAmbientPedPopulationZoneField::PopulationType))
    {
        zoneInfo.populationFlags = static_cast<unsigned char>((zoneInfo.populationFlags & ~0x1F) | state.populationType);
        expected.populationFlags = static_cast<unsigned char>((expected.populationFlags & ~0x1F) | state.populationType);
    }
    if (hasField(EAmbientPedPopulationZoneField::Races))
    {
        zoneInfo.raceFlags = static_cast<unsigned char>((zoneInfo.raceFlags & 0xF0) | state.races);
        expected.raceFlags = static_cast<unsigned char>((expected.raceFlags & 0xF0) | state.races);
    }
    if (hasField(EAmbientPedPopulationZoneField::DealerStrength))
    {
        zoneInfo.dealerStrength = state.dealerStrength;
        expected.dealerStrength = state.dealerStrength;
    }
    if (hasField(EAmbientPedPopulationZoneField::NoCops))
    {
        zoneInfo.populationFlags = static_cast<unsigned char>((zoneInfo.populationFlags & ~0x80) | (state.noCops ? 0x80 : 0));
        expected.populationFlags = static_cast<unsigned char>((expected.populationFlags & ~0x80) | (state.noCops ? 0x80 : 0));
    }
    for (unsigned int gangId = 0; gangId < AMBIENT_PED_GANG_COUNT; ++gangId)
    {
        if ((state.gangMask & (1u << gangId)) != 0)
        {
            zoneInfo.gangStrength[gangId] = state.gangStrength[gangId];
            expected.gangStrength[gangId] = state.gangStrength[gangId];
        }
    }

    *reinterpret_cast<int*>(GTA_CURRENT_STREAMING_ZONE_TYPE) = -1;
    return true;
}

void CGameSA::RestoreAmbientPedPopulationZones()
{
    if (!m_areAmbientPedPopulationZonesInitialized)
        return;

    auto* const zoneInfos = reinterpret_cast<SAmbientPedPopulationZoneInfoSA*>(GTA_ZONE_INFO_ARRAY);
    const auto* navigationZones = reinterpret_cast<const SAmbientPedNavigationZoneSA*>(GTA_NAVIGATION_ZONE_ARRAY);
    for (const auto& profile : g_ambientPedPopulationZoneProfiles)
    {
        const short zoneIndex = reinterpret_cast<short(__cdecl*)(const char*, int)>(FUNC_FindZoneByLabel)(profile.label, ZONE_TYPE_INFO);
        if (zoneIndex < 0 || zoneIndex >= 380)
            continue;

        const short zoneInfoIndex = navigationZones[zoneIndex].zoneInfoIndex;
        if (zoneInfoIndex < 0 || zoneInfoIndex >= 380)
            continue;

        auto&       zoneInfo = zoneInfos[zoneInfoIndex];
        const auto& snapshot = *reinterpret_cast<const SAmbientPedPopulationZoneInfoSA*>(m_ambientPedPopulationZoneSnapshot[zoneInfoIndex]);
        const auto& expected = *reinterpret_cast<const SAmbientPedPopulationZoneInfoSA*>(m_ambientPedPopulationZoneExpected[zoneInfoIndex]);
        for (unsigned int gangId = 0; gangId < AMBIENT_PED_GANG_COUNT; ++gangId)
        {
            if (zoneInfo.gangStrength[gangId] == expected.gangStrength[gangId])
                zoneInfo.gangStrength[gangId] = snapshot.gangStrength[gangId];
        }
        if (zoneInfo.dealerStrength == expected.dealerStrength)
            zoneInfo.dealerStrength = snapshot.dealerStrength;
        if ((zoneInfo.populationFlags & 0x1F) == (expected.populationFlags & 0x1F))
            zoneInfo.populationFlags = static_cast<unsigned char>((zoneInfo.populationFlags & ~0x1F) | (snapshot.populationFlags & 0x1F));
        if ((zoneInfo.populationFlags & 0x80) == (expected.populationFlags & 0x80))
            zoneInfo.populationFlags = static_cast<unsigned char>((zoneInfo.populationFlags & ~0x80) | (snapshot.populationFlags & 0x80));
        if ((zoneInfo.raceFlags & 0x0F) == (expected.raceFlags & 0x0F))
            zoneInfo.raceFlags = static_cast<unsigned char>((zoneInfo.raceFlags & 0xF0) | (snapshot.raceFlags & 0x0F));
    }
    m_areAmbientPedPopulationZonesInitialized = false;
}

void CGameSA::UpdateAmbientPedDealerModel()
{
    for (const int modelId : AMBIENT_PED_DEALER_MODELS)
        m_ambientPedPopulationStreamingTouched[modelId] = true;

    int                            desiredModel = -1;
    const auto* const              zoneInfo = *reinterpret_cast<SAmbientPedPopulationZoneInfoSA**>(GTA_CURRENT_POPCYCLE_ZONE_INFO);
    const int                      zoneType = *reinterpret_cast<const int*>(GTA_POPCYCLE_ZONE_TYPE);
    const int                      timeIndex = *reinterpret_cast<const int*>(GTA_POPCYCLE_TIME_INDEX);
    const int                      weekend = *reinterpret_cast<const int*>(GTA_POPCYCLE_WEEKEND);
    SAmbientPedPopulationTargetsSA targets;
    if (CalculateAmbientPedPopulationTargets(zoneInfo, timeIndex, weekend, zoneType, targets) && targets.dealer > 0.03f)
    {
        // StreamVehiclesAndPeds chooses exactly one entry from the DEALERS
        // group. Keep this ped-only passage separate: the surrounding retail
        // function also streams wanted-level police vehicles and actors.
        unsigned int dealerIndex = 2;
        if (*reinterpret_cast<const short*>(GTA_WEATHER_REGION) == WEATHER_REGION_SF)
            dealerIndex = 3;
        else if ((zoneInfo->raceFlags & 0x1) != 0)
            dealerIndex = 0;
        else if ((zoneInfo->raceFlags & 0x2) != 0)
            dealerIndex = 1;
        desiredModel = AMBIENT_PED_DEALER_MODELS[dealerIndex];
    }

    if (desiredModel >= 0)
        m_pStreaming->RequestModel(desiredModel, STREAMING_GAME_REQUIRED);

    for (const int modelId : AMBIENT_PED_DEALER_MODELS)
    {
        if (modelId == desiredModel)
            continue;
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelIsDeletable)(modelId);
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelTxdIsDeletable)(modelId);
    }
    m_ambientPedDealerModel = desiredModel;
}

void CGameSA::ResetAmbientPedDealerModel()
{
    for (const int modelId : AMBIENT_PED_DEALER_MODELS)
    {
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelIsDeletable)(modelId);
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelTxdIsDeletable)(modelId);
    }
    m_ambientPedDealerModel = -1;
}

void CGameSA::UpdateAmbientPedCopModel()
{
    for (const int modelId : AMBIENT_PED_COP_MODELS)
        m_ambientPedPopulationStreamingTouched[modelId] = true;

    int       desiredModel = -1;
    const int currentLevel = *reinterpret_cast<const unsigned char*>(GTA_CURRENT_LEVEL);
    if (currentLevel >= 0 && currentLevel < static_cast<int>(std::size(AMBIENT_PED_COP_MODELS)))
    {
        // StreamCopModels also consults wanted state, alternates a bike cop and
        // requests police vehicles. The traffic lease needs only the regional
        // city-cop model, so reproduce that one reversible request here.
        desiredModel = AMBIENT_PED_COP_MODELS[currentLevel];
        m_pStreaming->RequestModel(desiredModel, STREAMING_GAME_REQUIRED);
    }

    for (const int modelId : AMBIENT_PED_COP_MODELS)
    {
        if (modelId == desiredModel)
            continue;
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelIsDeletable)(modelId);
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelTxdIsDeletable)(modelId);
    }
    m_ambientPedCopModel = desiredModel;
}

void CGameSA::ResetAmbientPedCopModel()
{
    for (const int modelId : AMBIENT_PED_COP_MODELS)
    {
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelIsDeletable)(modelId);
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelTxdIsDeletable)(modelId);
    }
    m_ambientPedCopModel = -1;
}

void CGameSA::UpdateAmbientPedGangModels()
{
    const auto* const zoneInfo = *reinterpret_cast<SAmbientPedPopulationZoneInfoSA**>(GTA_CURRENT_POPCYCLE_ZONE_INFO);
    if (!zoneInfo)
        return;

    const bool rotate = ++m_ambientPedGangModelUpdateCounter >= AMBIENT_PED_GANG_ROTATION_TICKS;
    if (rotate)
    {
        m_ambientPedGangModelUpdateCounter = 0;
        m_ambientPedGangModelRotation = (m_ambientPedGangModelRotation + 1) % POPCYCLE_PED_GROUP_CAPACITY;
    }

    const auto* translations = reinterpret_cast<const SAmbientPedGroupTranslationSA*>(GTA_PED_GROUP_TRANSLATION);
    const auto* groupCounts = reinterpret_cast<const unsigned short*>(GTA_PED_GROUP_COUNTS);
    const auto* groupModels = reinterpret_cast<const short*>(GTA_PED_GROUP_MODELS);

    const auto releaseModel = [](int modelId)
    {
        if (modelId < 0)
            return;
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelIsDeletable)(modelId);
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelTxdIsDeletable)(modelId);
    };

    for (unsigned int gangId = 0; gangId < AMBIENT_PED_GANG_COUNT; ++gangId)
    {
        auto* currentModels = m_ambientPedGangModels[gangId];
        if (zoneInfo->gangStrength[gangId] == 0)
        {
            releaseModel(currentModels[0]);
            if (currentModels[1] != currentModels[0])
                releaseModel(currentModels[1]);
            currentModels[0] = currentModels[1] = -1;
            continue;
        }

        const unsigned int group = POPCYCLE_GANG_GROUP_BASE + gangId;
        if (group >= POPCYCLE_GROUP_COUNT)
            continue;
        const int worldZone = *reinterpret_cast<const int*>(GTA_CURRENT_WORLD_ZONE);
        if (worldZone < 0 || worldZone >= 3)
            continue;
        // GTA keeps the common entry count in world-zone slot zero, but reads
        // the actual models from the current LS/SF/LV pedgrp.dat column. Using
        // slot zero for both silently starves regional gangs outside LS.
        const int countPedGroup = translations[group].pedGroupIds[0];
        const int modelPedGroup = translations[group].pedGroupIds[worldZone];
        if (countPedGroup < 0 || modelPedGroup < 0 || static_cast<unsigned int>(countPedGroup) >= POPCYCLE_PED_GROUP_COUNT ||
            static_cast<unsigned int>(modelPedGroup) >= POPCYCLE_PED_GROUP_COUNT)
        {
            continue;
        }
        const unsigned int modelCount = std::min<unsigned int>(groupCounts[countPedGroup], POPCYCLE_PED_GROUP_CAPACITY);
        if (modelCount == 0)
            continue;

        int desiredModels[AMBIENT_PED_GANG_MODELS_PER_GANG] = {
            groupModels[modelPedGroup * POPCYCLE_PED_GROUP_CAPACITY + m_ambientPedGangModelRotation % modelCount],
            modelCount > 1 ? groupModels[modelPedGroup * POPCYCLE_PED_GROUP_CAPACITY + (m_ambientPedGangModelRotation + 1) % modelCount] : -1,
        };

        if (!rotate && currentModels[0] >= 0)
            continue;

        for (int desiredModel : desiredModels)
        {
            if (desiredModel >= 7 && desiredModel <= 288 && desiredModel != currentModels[0] && desiredModel != currentModels[1])
            {
                // The target ABI at 0x409C10 releases GAME_REQUIRED only. A
                // KEEP_IN_MEMORY request would survive our reversible lease
                // because neither the model nor TXD cleanup call clears it.
                m_ambientPedPopulationStreamingTouched[desiredModel] = true;
                m_pStreaming->RequestModel(desiredModel, STREAMING_GAME_REQUIRED);
            }
        }
        for (int currentModel : {currentModels[0], currentModels[1]})
        {
            if (currentModel >= 0 && currentModel != desiredModels[0] && currentModel != desiredModels[1])
                releaseModel(currentModel);
        }
        currentModels[0] = desiredModels[0];
        currentModels[1] = desiredModels[1];
    }
}

void CGameSA::ResetAmbientPedGangModels()
{
    for (auto& gangModels : m_ambientPedGangModels)
    {
        for (int& modelId : gangModels)
        {
            if (modelId >= 0)
            {
                reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelIsDeletable)(modelId);
                reinterpret_cast<void(__cdecl*)(int)>(FUNC_SetModelTxdIsDeletable)(modelId);
            }
            modelId = -1;
        }
    }
    m_ambientPedGangModelUpdateCounter = 0;
    m_ambientPedGangModelRotation = 0;
}

bool CGameSA::GetAmbientPedPopulationProfile(SAmbientPedPopulationProfile& profile) const
{
    profile = {};
    if (!m_areAmbientPedPopulationModelsActive || !*reinterpret_cast<void**>(GTA_CURRENT_POPCYCLE_ZONE) ||
        !*reinterpret_cast<void**>(GTA_CURRENT_POPCYCLE_ZONE_INFO))
        return false;

    const float pedDensityMultiplier = *reinterpret_cast<const float*>(GTA_PED_DENSITY_MULTIPLIER);
    const auto  maximumPedsInUse = *reinterpret_cast<const unsigned int*>(GTA_MAX_PEDS_IN_USE);
    const float creationDistanceMultiplier = reinterpret_cast<float(__cdecl*)()>(FUNC_PedCreationDistMultiplier)();
    const float generationDistanceMultiplier = *reinterpret_cast<const float*>(GTA_CAMERA_GENERATION_DISTANCE_MULTIPLIER);
    const int   zoneType = *reinterpret_cast<const int*>(GTA_POPCYCLE_ZONE_TYPE);
    const int   timeIndex = *reinterpret_cast<const int*>(GTA_POPCYCLE_TIME_INDEX);
    const int   weekend = *reinterpret_cast<const int*>(GTA_POPCYCLE_WEEKEND);
    const auto* zone = *reinterpret_cast<const SAmbientPedNavigationZoneSA**>(GTA_CURRENT_POPCYCLE_ZONE);
    const auto* zoneInfo = *reinterpret_cast<SAmbientPedPopulationZoneInfoSA**>(GTA_CURRENT_POPCYCLE_ZONE_INFO);
    const auto  zoneAddress = reinterpret_cast<std::uintptr_t>(zone);
    if (zoneAddress < GTA_NAVIGATION_ZONE_ARRAY || zoneAddress >= GTA_NAVIGATION_ZONE_ARRAY + 380 * sizeof(SAmbientPedNavigationZoneSA) ||
        (zoneAddress - GTA_NAVIGATION_ZONE_ARRAY) % sizeof(SAmbientPedNavigationZoneSA) != 0)
    {
        return false;
    }
    SAmbientPedPopulationTargetsSA targets;
    if (!CalculateAmbientPedPopulationTargets(zoneInfo, timeIndex, weekend, zoneType, targets) || !std::isfinite(pedDensityMultiplier) ||
        !std::isfinite(creationDistanceMultiplier) || !std::isfinite(generationDistanceMultiplier) || targets.civilian < 0.0f || targets.civilian > 110.0f ||
        targets.cop < 0.0f || targets.cop > 110.0f || targets.gang < 0.0f || targets.gang > 110.0f || targets.dealer < 0.0f || targets.dealer > 110.0f ||
        pedDensityMultiplier < 0.0f || pedDensityMultiplier > 10.0f || maximumPedsInUse > 110 || creationDistanceMultiplier < 1.0f ||
        creationDistanceMultiplier > 1.5f || generationDistanceMultiplier <= 0.0f || generationDistanceMultiplier > 10.0f || zoneType < 0 || zoneType >= 20 ||
        timeIndex < 0 || timeIndex >= 12 || weekend < 0 || weekend > 1)
    {
        return false;
    }

    profile.civilianTarget = targets.civilian;
    profile.gangTarget = targets.gang;
    profile.dealerTarget = targets.dealer;
    profile.pedDensityMultiplier = pedDensityMultiplier;
    profile.fewerPedsMultiplier = reinterpret_cast<bool(__cdecl*)()>(FUNC_CullZonesFewerPeds)() ? 0.6f : 1.0f;
    profile.maximumPedsInUse = maximumPedsInUse;
    profile.creationDistanceMultiplier = creationDistanceMultiplier;
    profile.generationDistanceMultiplier = generationDistanceMultiplier;
    profile.zoneType = static_cast<unsigned char>(zoneType);
    profile.timeIndex = static_cast<unsigned char>(timeIndex);
    profile.weekend = static_cast<unsigned char>(weekend);
    profile.dealerStrength = zoneInfo->dealerStrength;
    profile.raceFlags = zoneInfo->raceFlags & 0x0F;
    profile.noCops = (zoneInfo->populationFlags & 0x80) != 0;
    const int currentLevel = *reinterpret_cast<const unsigned char*>(GTA_CURRENT_LEVEL);
    if (currentLevel < 0 || currentLevel >= static_cast<int>(std::size(AMBIENT_PED_COP_MODELS)))
        return false;
    profile.worldLevel = static_cast<unsigned char>(currentLevel);
    if (profile.noCops)
        profile.copSuppressionFlags |= static_cast<unsigned char>(EAmbientPedCopSuppression::ZoneNoCops);
    if (*reinterpret_cast<const bool*>(GTA_DONT_CREATE_RANDOM_COPS))
        profile.copSuppressionFlags |= static_cast<unsigned char>(EAmbientPedCopSuppression::RandomCopsDisabled);
    if (reinterpret_cast<bool(__cdecl*)()>(FUNC_GangWarFightingGoingOn)())
        profile.copSuppressionFlags |= static_cast<unsigned char>(EAmbientPedCopSuppression::GangWarFighting);
    if (m_ambientPedPopulationOriginZ >= 950.0f)
        profile.copSuppressionFlags |= static_cast<unsigned char>(EAmbientPedCopSuppression::HighAltitude);
    profile.rawCopTarget = targets.cop;
    profile.copTarget = profile.copSuppressionFlags == 0 ? targets.cop : 0.0f;
    profile.supportedTarget = targets.civilian + targets.gang + targets.dealer + profile.copTarget;
    profile.target = profile.supportedTarget;
    std::copy(std::begin(zoneInfo->gangStrength), std::end(zoneInfo->gangStrength), std::begin(profile.gangWeights));
    std::copy(std::begin(zone->infoLabel), std::end(zone->infoLabel), std::begin(profile.zoneLabel));
    return std::isfinite(profile.target) && std::isfinite(profile.supportedTarget) && profile.target <= 110.0f;
}

bool CGameSA::IsAmbientPedSphereVisible(const CVector& position, float radius)
{
    if (!std::isfinite(position.fX) || !std::isfinite(position.fY) || !std::isfinite(position.fZ) || !std::isfinite(radius) || radius < 0.0f || radius > 100.0f)
    {
        return false;
    }
    CVector mutablePosition = position;
    return m_pCamera->IsSphereVisible(&mutablePosition, radius);
}

EAmbientVehicleModelCandidateResult CGameSA::GetAmbientVehicleModelCandidate(SAmbientVehicleModelCandidate& candidate)
{
    candidate = {};
    if (!*reinterpret_cast<void**>(GTA_CURRENT_POPCYCLE_ZONE_INFO))
        return EAmbientVehicleModelCandidateResult::PopulationUnavailable;

    // GTA uses this exact popcycle-weighted selector before reading cargrp.dat.
    // The later retail loaded-car chooser is intentionally not used: MTA
    // disables the ambient vehicle streaming loop which maintains that pool.
    const int     carGroup = reinterpret_cast<int(__cdecl*)()>(FUNC_PickARandomGroupOfOtherPeds)();
    constexpr int CAR_GROUP_COUNT = 18;
    constexpr int CAR_GROUP_CAPACITY = 23;
    if (carGroup < 0 || carGroup >= CAR_GROUP_COUNT)
        return EAmbientVehicleModelCandidateResult::InvalidGroup;

    const auto* counts = reinterpret_cast<const short*>(GTA_CAR_GROUP_COUNTS);
    const auto* models = reinterpret_cast<const short*>(GTA_CAR_GROUP_MODELS);
    const int   count = counts[carGroup];
    if (count <= 0 || count > CAR_GROUP_CAPACITY)
        return EAmbientVehicleModelCandidateResult::InvalidGroup;

    const int start = rand() % count;
    for (int offset = 0; offset < count; ++offset)
    {
        const int modelId = models[carGroup * CAR_GROUP_CAPACITY + (start + offset) % count];
        if (modelId < 400 || modelId > 611)
            continue;
        CModelInfo* const modelInfo = GetModelInfo(modelId);
        if (!modelInfo || !modelInfo->IsVehicle())
            continue;
        const auto vehicleClass = static_cast<VehicleClass>(modelInfo->GetVehicleType());
        if (vehicleClass != VehicleClass::AUTOMOBILE && vehicleClass != VehicleClass::MONSTER_TRUCK && vehicleClass != VehicleClass::QUAD &&
            vehicleClass != VehicleClass::BIKE && vehicleClass != VehicleClass::BMX)
            continue;

        candidate.modelId = modelId;
        candidate.carGroup = static_cast<unsigned char>(carGroup);
        candidate.vehicleClass = static_cast<unsigned char>(vehicleClass);
        return EAmbientVehicleModelCandidateResult::Success;
    }
    return EAmbientVehicleModelCandidateResult::NoRoadModel;
}

bool CGameSA::GetAmbientVehicleOccupantModelCandidate(unsigned int vehicleModelId, unsigned int maximumOccupants,
                                                      SAmbientVehicleOccupantModelCandidate& candidate)
{
    candidate = {};
    if (vehicleModelId < 400 || vehicleModelId > 611 || maximumOccupants == 0 || maximumOccupants > AMBIENT_VEHICLE_MAX_OCCUPANTS)
        return false;

    const auto* vehicleModelInfo = reinterpret_cast<const unsigned char*>(CModelInfoSAInterface::GetModelInfo(vehicleModelId));
    if (!vehicleModelInfo)
        return false;
    const int vehicleClass = *reinterpret_cast<const signed char*>(vehicleModelInfo + 0x4D);
    if (vehicleClass < 0 || vehicleClass > 11)
        return false;

    const auto isCompatible =
        [this, vehicleClass](int modelId, int referenceType, bool requireUnique, const SAmbientVehicleOccupantModelCandidate& output, bool requireLoaded)
    {
        CModelInfo* const modelInfo = modelId >= 7 && modelId <= 288 ? GetModelInfo(modelId) : nullptr;
        const auto*       streamingInfo = modelInfo ? m_pStreaming->GetStreamingInfo(modelId) : nullptr;
        if (!modelInfo || modelInfo->GetModelType() != eModelInfoType::PED ||
            (requireLoaded && (!streamingInfo || streamingInfo->loadState != eModelLoadState::LOADSTATE_LOADED)))
            return -1;
        const auto* pedModelInfo = reinterpret_cast<const unsigned char*>(CModelInfoSAInterface::GetModelInfo(modelId));
        if (!pedModelInfo || (referenceType < 4 && *reinterpret_cast<const short*>(pedModelInfo + 0x08) != referenceType) ||
            (*reinterpret_cast<const unsigned short*>(pedModelInfo + 0x30) & (1u << vehicleClass)) == 0 ||
            !reinterpret_cast<bool(__cdecl*)(int)>(FUNC_PedIsAcceptableInCurrentZone)(modelId))
            return -1;
        if (requireUnique)
        {
            for (unsigned int index = 0; index < output.count; ++index)
            {
                if (output.modelIds[index] == static_cast<unsigned int>(modelId))
                    return -1;
            }
        }
        return modelId;
    };

    // The fifth reference pass is the catch-all used by the exact executable;
    // gta-reversed incorrectly stops the loop before it.
    const auto* loadedPedModels = reinterpret_cast<const int*>(GTA_LOADED_PED_MODELS);
    for (bool requireUnique : {true, false})
    {
        for (int referenceType = 0; referenceType <= 4 && candidate.count < maximumOccupants; ++referenceType)
        {
            for (int slot = 0; slot < 8 && candidate.count < maximumOccupants; ++slot)
            {
                const int modelId = isCompatible(loadedPedModels[slot], referenceType, requireUnique, candidate, true);
                if (modelId >= 0)
                    candidate.modelIds[candidate.count++] = static_cast<unsigned int>(modelId);
            }
        }
        if (candidate.count >= maximumOccupants)
            break;
    }

    // MTA disables the retail ambient vehicle streamer, so the eight ped
    // slots can legitimately contain no model compatible with a van or bike.
    // The network creation transaction explicitly leases its chosen models;
    // scan stock ped definitions only after exhausting the exact loaded-slot
    // selector so every road class still has an atomic occupant proposal.
    const int stockPedStart = 7 + rand() % (289 - 7);
    for (bool requireUnique : {true, false})
    {
        for (int referenceType = 0; referenceType <= 4 && candidate.count < maximumOccupants; ++referenceType)
        {
            for (int offset = 0; offset < 289 - 7 && candidate.count < maximumOccupants; ++offset)
            {
                const int modelId = 7 + (stockPedStart - 7 + offset) % (289 - 7);
                const int compatibleModelId = isCompatible(modelId, referenceType, requireUnique, candidate, false);
                if (compatibleModelId >= 0)
                    candidate.modelIds[candidate.count++] = static_cast<unsigned int>(compatibleModelId);
            }
        }
        if (candidate.count >= maximumOccupants)
            break;
    }
    return candidate.count > 0;
}

EAmbientVehicleSpawnCandidateResult CGameSA::GetAmbientVehicleSpawnCandidate(const CVector& origin, unsigned int modelId,
                                                                             SAmbientVehicleSpawnCandidate& candidate)
{
    candidate = {};
    if (!std::isfinite(origin.fX) || !std::isfinite(origin.fY) || !std::isfinite(origin.fZ))
        return EAmbientVehicleSpawnCandidateResult::InvalidOrigin;

    CModelInfo* const modelInfo = GetModelInfo(modelId);
    if (!modelInfo || !modelInfo->IsVehicle())
        return EAmbientVehicleSpawnCandidateResult::UnsupportedModel;
    const auto vehicleClass = static_cast<VehicleClass>(modelInfo->GetVehicleType());
    if (vehicleClass != VehicleClass::AUTOMOBILE && vehicleClass != VehicleClass::MONSTER_TRUCK && vehicleClass != VehicleClass::QUAD &&
        vehicleClass != VehicleClass::BIKE && vehicleClass != VehicleClass::BMX)
        return EAmbientVehicleSpawnCandidateResult::UnsupportedModel;

    float       directionX = *reinterpret_cast<const float*>(GTA_CAMERA_FORWARD_X);
    float       directionY = *reinterpret_cast<const float*>(GTA_CAMERA_FORWARD_Y);
    const float directionLength = std::sqrt(directionX * directionX + directionY * directionY);
    if (!std::isfinite(directionLength) || directionLength < 0.001f)
    {
        directionX = 0.0f;
        directionY = 1.0f;
    }
    else
    {
        directionX /= directionLength;
        directionY /= directionLength;
    }

    const float generationMultiplier = *reinterpret_cast<const float*>(GTA_CAMERA_GENERATION_DISTANCE_MULTIPLIER);
    const float generationBaseDistance = *reinterpret_cast<const float*>(0x858970);
    if (!std::isfinite(generationMultiplier) || !std::isfinite(generationBaseDistance) || generationMultiplier <= 0.0f || generationBaseDistance <= 0.0f)
    {
        return EAmbientVehicleSpawnCandidateResult::InvalidOrigin;
    }

    CVector                      position{};
    SAmbientVehicleNodeAddressSA nodeA{};
    SAmbientVehicleNodeAddressSA nodeB{};
    float                        pathLerp{};
    using GenerateCarCreationCoors2 = bool(__cdecl*)(CVector, float, float, float, bool, float, float, CVector*, SAmbientVehicleNodeAddressSA*,
                                                     SAmbientVehicleNodeAddressSA*, float*, bool, bool);
    const auto generate = reinterpret_cast<GenerateCarCreationCoors2>(FUNC_GenerateCarCreationCoors2);
    bool generated = generate(origin, directionX, directionY, -1.0f, true, generationMultiplier * generationBaseDistance, 38.0f, &position, &nodeA, &nodeB,
                              &pathLerp, true, false);
    if (!generated)
    {
        // Retail calls this probabilistic oracle twice per frame indefinitely.
        // Neon has a bounded server request, so probe three additional camera
        // sectors with a wider inner ring before reporting a normal miss.
        constexpr float FALLBACK_ANGLES[] = {1.0471975512f, -1.0471975512f, 3.1415926536f};
        for (float angle : FALLBACK_ANGLES)
        {
            const float rotatedX = directionX * std::cos(angle) - directionY * std::sin(angle);
            const float rotatedY = directionX * std::sin(angle) + directionY * std::cos(angle);
            generated = generate(origin, rotatedX, rotatedY, -1.0f, true, generationMultiplier * generationBaseDistance, 70.0f, &position, &nodeA, &nodeB,
                                 &pathLerp, true, false);
            if (generated)
                break;
        }
    }
    if (!generated)
    {
        // GenerateCarCreationCoors2 keeps two low-traffic and two ordinary
        // cached starting nodes. Distinguish an empty streamed path area from
        // a valid graph where no point satisfied the retail visibility gates.
        constexpr std::uintptr_t CACHED_NODE_ADDRESSES[] = {0x969104, 0x969100, 0x9690FC, 0x9690F8};
        for (const auto address : CACHED_NODE_ADDRESSES)
        {
            CVector cachedPosition{};
            bool    cachedIsWater{};
            if (GetAmbientVehiclePathNodePosition(*reinterpret_cast<const SAmbientVehicleNodeAddressSA*>(address), cachedPosition, cachedIsWater))
                return EAmbientVehicleSpawnCandidateResult::NoPath;
        }
        return EAmbientVehicleSpawnCandidateResult::InvalidPathNode;
    }
    if (!std::isfinite(position.fX) || !std::isfinite(position.fY) || !std::isfinite(position.fZ) || !std::isfinite(pathLerp) || pathLerp < -0.01f ||
        pathLerp > 1.01f)
    {
        return EAmbientVehicleSpawnCandidateResult::InvalidOutput;
    }

    CVector pathStart{};
    CVector pathEnd{};
    bool    startIsWater{};
    bool    endIsWater{};
    if (!GetAmbientVehiclePathNodePosition(nodeA, pathStart, startIsWater) || !GetAmbientVehiclePathNodePosition(nodeB, pathEnd, endIsWater))
        return EAmbientVehicleSpawnCandidateResult::InvalidPathNode;
    if (startIsWater || endIsWater)
        return EAmbientVehicleSpawnCandidateResult::WaterPath;

    const float deltaX = pathEnd.fX - pathStart.fX;
    const float deltaY = pathEnd.fY - pathStart.fY;
    const float pathLength = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(pathLength) || pathLength < 0.1f)
        return EAmbientVehicleSpawnCandidateResult::InvalidPathNode;

    float laneOffset = 0.0f;
    if (!GetAmbientVehicleLaneOffset(nodeA, nodeB, modelId, vehicleClass, laneOffset))
        return EAmbientVehicleSpawnCandidateResult::NoPath;
    const float directionXOnRoad = deltaX / pathLength;
    const float directionYOnRoad = deltaY / pathLength;
    position.fX += laneOffset * directionYOnRoad;
    position.fY -= laneOffset * directionXOnRoad;

    const float pathHeight = pathStart.fZ + (pathEnd.fZ - pathStart.fZ) * pathLerp;
    bool        hasGround = false;
    const float groundZ = reinterpret_cast<float(__cdecl*)(float, float, float, bool*, void**)>(FUNC_FindAmbientGroupGroundZ)(
        position.fX, position.fY, pathHeight + 4.0f, &hasGround, nullptr);
    const float centreToBase = modelInfo->GetDistanceFromCentreOfMassToBaseOfModel();
    if (!hasGround || !std::isfinite(pathHeight) || !std::isfinite(groundZ) || !std::isfinite(centreToBase) || centreToBase < 0.0f || centreToBase > 10.0f)
    {
        return EAmbientVehicleSpawnCandidateResult::GroundMissing;
    }

    constexpr float RADIANS_TO_DEGREES = 57.29577951308232f;
    float           rotation = std::atan2(-deltaX, deltaY) * RADIANS_TO_DEGREES;
    if (rotation < 0.0f)
        rotation += 360.0f;

    // Retail resolves the road candidate back to collision ground before
    // adding the selected model's centre-to-base offset. A fixed lift makes
    // different cars float or intersect sloped roads and bridge decks.
    candidate.position = CVector(position.fX, position.fY, groundZ + centreToBase);
    candidate.rotationDegrees = rotation;
    candidate.modelId = modelId;

    // GenerateOneRandomCar stores an integer cruise speed. Preserve its
    // vehicle-list ranges and reductions before transporting the scalar to the
    // owner-local Wander task.
    constexpr unsigned char VEHICLE_LIST_POOR_FAMILY = 1;
    constexpr unsigned char VEHICLE_LIST_EXECUTIVE = 3;
    constexpr unsigned char VEHICLE_LIST_BIG = 5;
    constexpr unsigned int  MODEL_TRACTOR = 531;
    constexpr unsigned int  MODEL_COMBINE = 532;
    const auto*             vehicleModelInfo = reinterpret_cast<const CVehicleModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(modelId));
    const unsigned char     vehicleList = vehicleModelInfo ? vehicleModelInfo->vehicleClass : 0xFF;

    unsigned int        cruiseSpeed = vehicleList == VEHICLE_LIST_EXECUTIVE     ? 18 + rand() % 9
                                      : vehicleList == VEHICLE_LIST_POOR_FAMILY ? 10 + rand() % 5
                                                                                : 13 + rand() % 8;
    const CBoundingBox* boundingBox = modelInfo->GetBoundingBox();
    const float         vehicleLength = boundingBox ? boundingBox->vecBoundMax.fY - boundingBox->vecBoundMin.fY : 0.0f;
    if ((std::isfinite(vehicleLength) && vehicleLength > 10.0f) || vehicleList == VEHICLE_LIST_BIG)
        cruiseSpeed = cruiseSpeed * 3 / 4;
    if (modelId == MODEL_TRACTOR || modelId == MODEL_COMBINE || vehicleClass == VehicleClass::BMX)
        cruiseSpeed /= 3;

    candidate.cruiseSpeed = static_cast<float>(cruiseSpeed);
    candidate.vehicleClass = static_cast<unsigned char>(vehicleClass);
    candidate.drivingStyle = vehicleClass == VehicleClass::BIKE ? 6 : 0;
    return EAmbientVehicleSpawnCandidateResult::Success;
}

EAmbientPedSpawnCandidateResult CGameSA::GetAmbientPedSpawnCandidate(const CVector& origin, SAmbientPedSpawnCandidate& candidate)
{
    return GetAmbientPedSpawnCandidateForPopulation(origin, EAmbientPedPopulationSelection::Automatic, 0xFF, candidate);
}

EAmbientPedSpawnCandidateResult CGameSA::GetAmbientPedSpawnCandidateForPopulation(const CVector& origin, EAmbientPedPopulationSelection selection,
                                                                                  unsigned char gangId, SAmbientPedSpawnCandidate& candidate)
{
    candidate = {};
    if (selection != EAmbientPedPopulationSelection::Automatic && selection != EAmbientPedPopulationSelection::Civilian &&
        selection != EAmbientPedPopulationSelection::Gang && selection != EAmbientPedPopulationSelection::Dealer &&
        selection != EAmbientPedPopulationSelection::Cop)
    {
        return EAmbientPedSpawnCandidateResult::NoModel;
    }
    if (selection == EAmbientPedPopulationSelection::Gang && gangId >= AMBIENT_PED_GANG_COUNT)
        return EAmbientPedSpawnCandidateResult::NoModel;
    if (!std::isfinite(origin.fX) || !std::isfinite(origin.fY) || !std::isfinite(origin.fZ))
        return EAmbientPedSpawnCandidateResult::InvalidOrigin;

    const float distanceMultiplier = reinterpret_cast<float(__cdecl*)()>(FUNC_PedCreationDistMultiplier)();
    const float generationMultiplier = *reinterpret_cast<const float*>(GTA_CAMERA_GENERATION_DISTANCE_MULTIPLIER);
    if (!std::isfinite(distanceMultiplier) || !std::isfinite(generationMultiplier) || distanceMultiplier <= 0.0f || generationMultiplier <= 0.0f)
        return EAmbientPedSpawnCandidateResult::InvalidOrigin;

    const float visibleMinDistance = distanceMultiplier * generationMultiplier * 42.5f;
    const float visibleMaxDistance = distanceMultiplier * generationMultiplier * 50.5f;
    const float hiddenMinDistance = distanceMultiplier * 25.0f - 10.0f;
    const float hiddenMaxDistance = distanceMultiplier * 25.0f;

    int                          modelId = -1;
    CPedModelInfoSAInterface*    modelInfo = nullptr;
    SAmbientPedPopulationProfile profile;
    const bool                   hasProfile = GetAmbientPedPopulationProfile(profile);
    const float                  automaticTicket = selection == EAmbientPedPopulationSelection::Automatic && hasProfile && profile.supportedTarget > 0.0f
                                                       ? static_cast<float>(rand() & 0xFFFF) / 65535.0f * profile.supportedTarget
                                                       : -1.0f;
    const bool                   chooseDealer = selection == EAmbientPedPopulationSelection::Dealer ||
                              (selection == EAmbientPedPopulationSelection::Automatic && automaticTicket >= 0.0f && automaticTicket < profile.dealerTarget);
    const bool chooseGang = selection == EAmbientPedPopulationSelection::Gang ||
                            (selection == EAmbientPedPopulationSelection::Automatic && automaticTicket >= profile.dealerTarget &&
                             automaticTicket < profile.dealerTarget + profile.gangTarget);
    const bool chooseCop = selection == EAmbientPedPopulationSelection::Cop ||
                           (selection == EAmbientPedPopulationSelection::Automatic && automaticTicket >= profile.dealerTarget + profile.gangTarget &&
                            automaticTicket < profile.dealerTarget + profile.gangTarget + profile.copTarget);
    if (chooseDealer)
    {
        const int proposedModel = m_ambientPedDealerModel;
        if (proposedModel >= 0)
        {
            auto* proposedInfo = reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(proposedModel));
            if (proposedInfo && proposedInfo->pRwObject)
            {
                modelId = proposedModel;
                modelInfo = proposedInfo;
                candidate.populationClass = EAmbientPedPopulationClass::Dealer;
                candidate.gangId = 0xFF;
            }
        }
        if (selection == EAmbientPedPopulationSelection::Dealer && modelId < 0)
            return EAmbientPedSpawnCandidateResult::NoModel;
    }
    if (chooseGang)
    {
        const auto* const zoneInfo = *reinterpret_cast<SAmbientPedPopulationZoneInfoSA**>(GTA_CURRENT_POPCYCLE_ZONE_INFO);
        if (!zoneInfo)
            return EAmbientPedSpawnCandidateResult::NoModel;
        unsigned int selectedGangId = gangId;
        if (selection == EAmbientPedPopulationSelection::Automatic)
        {
            unsigned int totalStrength = 0;
            for (unsigned char strength : zoneInfo->gangStrength)
                totalStrength += strength;
            if (totalStrength > 0)
            {
                unsigned int ticket = static_cast<unsigned int>(rand()) % totalStrength;
                selectedGangId = 0;
                for (; selectedGangId < AMBIENT_PED_GANG_COUNT; ++selectedGangId)
                {
                    if (ticket < zoneInfo->gangStrength[selectedGangId])
                        break;
                    ticket -= zoneInfo->gangStrength[selectedGangId];
                }
            }
            else
                selectedGangId = AMBIENT_PED_GANG_COUNT;
        }

        if (selectedGangId < AMBIENT_PED_GANG_COUNT && zoneInfo->gangStrength[selectedGangId] > 0)
        {
            // The network-authoritative caller has already reproduced
            // FindNewPedType/PickGangToCreateMembersOf from shared live counts.
            // This call deliberately does not fall back to a civilian: a miss
            // must preserve that selection until the requested model is ready.
            const int proposedModel = reinterpret_cast<int(__cdecl*)(int)>(FUNC_ChooseGangOccupation)(selectedGangId);
            if (proposedModel >= 7 && proposedModel <= 288)
            {
                auto* proposedInfo = reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(proposedModel));
                if (proposedInfo && proposedInfo->pRwObject && proposedInfo->pedType == PED_TYPE_GANG1 + selectedGangId)
                {
                    modelId = proposedModel;
                    modelInfo = proposedInfo;
                    candidate.populationClass = EAmbientPedPopulationClass::Gang;
                    candidate.gangId = static_cast<unsigned char>(selectedGangId);
                }
            }
        }
        if (selection == EAmbientPedPopulationSelection::Gang && modelId < 0)
            return EAmbientPedSpawnCandidateResult::NoModel;
    }

    if (chooseCop)
    {
        const int currentLevel = *reinterpret_cast<const unsigned char*>(GTA_CURRENT_LEVEL);
        const int proposedModel = currentLevel >= 0 && currentLevel < static_cast<int>(std::size(AMBIENT_PED_COP_MODELS)) ? m_ambientPedCopModel : -1;
        if (proposedModel >= 0 && proposedModel == AMBIENT_PED_COP_MODELS[currentLevel])
        {
            auto* proposedInfo = reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(proposedModel));
            if (proposedInfo && proposedInfo->pRwObject && proposedInfo->pedType == PED_TYPE_COP)
            {
                modelId = proposedModel;
                modelInfo = proposedInfo;
                candidate.populationClass = EAmbientPedPopulationClass::Cop;
                candidate.gangId = 0xFF;
                candidate.worldLevel = static_cast<unsigned char>(currentLevel);
            }
        }
        if (selection == EAmbientPedPopulationSelection::Cop && modelId < 0)
            return EAmbientPedSpawnCandidateResult::NoModel;
    }

    if (modelId < 0 && selection != EAmbientPedPopulationSelection::Gang && selection != EAmbientPedPopulationSelection::Dealer &&
        selection != EAmbientPedPopulationSelection::Cop)
    {
        const auto chooseCivilian = reinterpret_cast<int(__cdecl*)(bool, bool, int, int, int, bool, bool, bool, const char*)>(FUNC_ChooseCivilianOccupation);
        modelId = chooseCivilian(false, false, -1, -1, -1, false, true, false, nullptr);
        modelInfo = modelId >= 0 ? reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(modelId)) : nullptr;
        if (!modelInfo || (modelInfo->pedType != PED_TYPE_CIVMALE && modelInfo->pedType != PED_TYPE_CIVFEMALE))
        {
            const bool tryMaleFirst = (rand() & 1) != 0;
            modelId = chooseCivilian(tryMaleFirst, !tryMaleFirst, -1, -1, -1, false, true, false, nullptr);
            if (modelId < 0)
                modelId = chooseCivilian(!tryMaleFirst, tryMaleFirst, -1, -1, -1, false, true, false, nullptr);
            modelInfo = modelId >= 0 ? reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(modelId)) : nullptr;
        }
        candidate.populationClass = EAmbientPedPopulationClass::Civilian;
        candidate.gangId = 0xFF;
    }
    if (modelId < 0)
        return EAmbientPedSpawnCandidateResult::NoModel;
    if (!modelInfo || !modelInfo->pRwObject ||
        (candidate.populationClass == EAmbientPedPopulationClass::Civilian && modelInfo->pedType != PED_TYPE_CIVMALE &&
         modelInfo->pedType != PED_TYPE_CIVFEMALE) ||
        (candidate.populationClass == EAmbientPedPopulationClass::Dealer &&
         std::find(std::begin(AMBIENT_PED_DEALER_MODELS), std::end(AMBIENT_PED_DEALER_MODELS), modelId) == std::end(AMBIENT_PED_DEALER_MODELS)) ||
        (candidate.populationClass == EAmbientPedPopulationClass::Cop &&
         (candidate.worldLevel >= std::size(AMBIENT_PED_COP_MODELS) || modelId != AMBIENT_PED_COP_MODELS[candidate.worldLevel] ||
          modelInfo->pedType != PED_TYPE_COP)))
    {
        return EAmbientPedSpawnCandidateResult::UnsupportedModel;
    }

    const float gangVisibleOffset = candidate.populationClass == EAmbientPedPopulationClass::Gang ? 30.0f : 0.0f;
    const float effectiveVisibleMinDistance = visibleMinDistance + gangVisibleOffset;
    const float effectiveVisibleMaxDistance = visibleMaxDistance + gangVisibleOffset;
    const float visibleTooCloseDistance = distanceMultiplier * 42.5f + gangVisibleOffset;

    CVector      position{};
    unsigned int firstNode{};
    unsigned int secondNode{};
    float        pathLerp{};
    const bool   generated =
        reinterpret_cast<bool(__thiscall*)(void*, float, float, float, float, float, float, CVector*, unsigned int*, unsigned int*, float*, bool, void*)>(
            FUNC_GeneratePedCreationCoors)(reinterpret_cast<void*>(GTA_PATH_FIND), origin.fX, origin.fY, effectiveVisibleMinDistance,
                                           effectiveVisibleMaxDistance, hiddenMinDistance, hiddenMaxDistance, &position, &firstNode, &secondNode, &pathLerp,
                                           false, nullptr);
    if (!generated)
        return EAmbientPedSpawnCandidateResult::NoPath;

    const auto getSpawnProbability = [](unsigned int nodeAddress, unsigned char& probability)
    {
        constexpr unsigned int PATH_NODE_AREA_COUNT = 64;
        constexpr unsigned int PATH_NODE_ARRAY_OFFSET = 0x804;
        constexpr unsigned int PATH_NODE_SIZE = 0x1C;
        constexpr unsigned int PATH_NODE_SPAWN_FLAGS_OFFSET = 0x1A;

        const auto area = nodeAddress & 0xFFFF;
        const auto node = nodeAddress >> 16;
        if (area >= PATH_NODE_AREA_COUNT || area == 0xFFFF || node == 0xFFFF)
            return false;

        auto* const* nodeAreas = reinterpret_cast<unsigned char* const*>(GTA_PATH_FIND + PATH_NODE_ARRAY_OFFSET);
        const auto*  nodeArray = nodeAreas[area];
        if (!nodeArray)
            return false;

        probability = nodeArray[node * PATH_NODE_SIZE + PATH_NODE_SPAWN_FLAGS_OFFSET] & 0xF;
        return true;
    };

    unsigned char firstProbability{};
    unsigned char secondProbability{};
    if (!getSpawnProbability(firstNode, firstProbability) || !getSpawnProbability(secondNode, secondProbability))
        return EAmbientPedSpawnCandidateResult::NoPath;
    if ((rand() & 0xF) > std::min(firstProbability, secondProbability))
        return EAmbientPedSpawnCandidateResult::PathDensity;

    const auto widthSeed = static_cast<unsigned short>(rand());
    reinterpret_cast<void(__thiscall*)(void*, unsigned int, unsigned int, unsigned short, float*, float*)>(FUNC_TakePathWidthIntoAccount)(
        reinterpret_cast<void*>(GTA_PATH_FIND), firstNode, secondNode, widthSeed, &position.fX, &position.fY);

    // The generic single-ped AddToPopulation branch lifts the grounded path
    // result by this amount; the +1 m probe belongs only to couple placement.
    position.fZ += 0.7f;

    // This is the same conservative collision query used by AddToPopulation.
    if (!reinterpret_cast<bool(__cdecl*)(const CVector&, float, int, void*, bool, bool, bool)>(FUNC_IsPositionClearForPed)(position, -1.0f, -1, nullptr, true,
                                                                                                                           true, true))
    {
        return EAmbientPedSpawnCandidateResult::Blocked;
    }

    const float deltaX = position.fX - origin.fX;
    const float deltaY = position.fY - origin.fY;
    if (m_pCamera->IsSphereVisible(&position, 2.0f) && std::sqrt(deltaX * deltaX + deltaY * deltaY) < visibleTooCloseDistance)
        return EAmbientPedSpawnCandidateResult::VisibleTooClose;

    candidate.position = position;
    candidate.modelId = static_cast<unsigned int>(modelId);
    candidate.pedType = candidate.populationClass == EAmbientPedPopulationClass::Dealer ? PED_TYPE_DEALER : static_cast<unsigned char>(modelInfo->pedType);
    candidate.wanderDirection = static_cast<unsigned char>(rand() & 7);
    candidate.pathLerp = pathLerp;
    return EAmbientPedSpawnCandidateResult::Success;
}

EAmbientPedSpawnCandidateResult CGameSA::GetAmbientPedGangGroupCandidate(const CVector& origin, unsigned char gangId, unsigned char maxMembers,
                                                                         SAmbientPedGroupSpawnCandidate& candidate)
{
    candidate = {};
    if (gangId >= AMBIENT_PED_GANG_COUNT || maxMembers < 2)
        return EAmbientPedSpawnCandidateResult::NoModel;

    SAmbientPedSpawnCandidate anchor;
    const auto                result = GetAmbientPedSpawnCandidateForPopulation(origin, EAmbientPedPopulationSelection::Gang, gangId, anchor);
    if (result != EAmbientPedSpawnCandidateResult::Success)
        return result;

    const unsigned int upperBound = std::min<unsigned int>({4, maxMembers, AMBIENT_PED_GROUP_MAX_MEMBERS});
    const unsigned int requestedCount = upperBound == 2 ? 2 : 2 + static_cast<unsigned int>(rand()) % (upperBound - 1);
    constexpr float    kAmbientGroupPi = 3.14159265358979323846f;
    const float        angleStep = 2.0f * kAmbientGroupPi / static_cast<float>(requestedCount);
    const float        placeRadius = std::sqrt(0.5f / (1.0f - std::cos(angleStep)));
    const CVector      groupOrigin = anchor.position;
    if (!reinterpret_cast<bool(__cdecl*)(const CVector&, float, int, void*, bool, bool, bool)>(FUNC_IsPositionClearForPed)(groupOrigin, placeRadius, -1,
                                                                                                                           nullptr, true, true, true))
    {
        return EAmbientPedSpawnCandidateResult::Blocked;
    }

    CVector firstGroundPosition{};
    bool    hasFirstGroundPosition = false;

    for (unsigned int index = 0; index < requestedCount; ++index)
    {
        const float angleJitter = (static_cast<float>(rand() & 0xFFFF) / 65535.0f * 0.4f - 0.2f) * angleStep;
        const float radiusJitter = static_cast<float>(rand() & 0xFFFF) / 65535.0f * 0.4f - 0.2f;
        const float angle = static_cast<float>(index) * angleStep + angleJitter;
        const float radius = placeRadius * (1.0f + radiusJitter);

        SAmbientPedSpawnCandidate member = anchor;
        auto*                     modelInfo = reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(member.modelId));
        if (index != 0)
        {
            const int modelId = reinterpret_cast<int(__cdecl*)(int)>(FUNC_ChooseGangOccupation)(gangId);
            modelInfo = modelId >= 0 ? reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(modelId)) : nullptr;
            if (!modelInfo || !modelInfo->pRwObject || modelInfo->pedType != PED_TYPE_GANG1 + gangId)
                continue;
            member.modelId = modelId;
            member.pedType = static_cast<unsigned char>(modelInfo->pedType);
            member.wanderDirection = static_cast<unsigned char>(rand() & 7);
        }

        member.position.fX = groupOrigin.fX + std::cos(angle) * radius;
        member.position.fY = groupOrigin.fY + std::sin(angle) * radius;
        member.position.fZ = groupOrigin.fZ + 1.0f;
        bool        hasGround = false;
        const float groundZ = reinterpret_cast<float(__cdecl*)(float, float, float, bool*, void**)>(FUNC_FindAmbientGroupGroundZ)(
            member.position.fX, member.position.fY, member.position.fZ, &hasGround, nullptr);
        if (!hasGround)
            continue;
        member.position.fZ = std::max(groupOrigin.fZ, groundZ + 1.0f);
        const float headingRadians = reinterpret_cast<float(__cdecl*)(float, float, float, float)>(FUNC_GetRadianAngleBetweenPoints)(
            groupOrigin.fX, groupOrigin.fY, member.position.fX, member.position.fY);
        member.headingDegrees = headingRadians * 180.0f / kAmbientGroupPi;
        if (!hasFirstGroundPosition)
        {
            firstGroundPosition = member.position;
            hasFirstGroundPosition = true;
        }

        const float boundRadius = modelInfo && modelInfo->pColModel ? modelInfo->pColModel->m_sphere.m_radius : -1.0f;
        if (!reinterpret_cast<bool(__cdecl*)(const CVector&, float, int, void*, bool, bool, bool)>(FUNC_IsPositionClearForPed)(member.position, boundRadius, -1,
                                                                                                                               nullptr, true, true, true))
        {
            continue;
        }
        if (hasFirstGroundPosition && index != 0)
        {
            SLineOfSightFlags flags;
            flags.bCheckVehicles = false;
            flags.bCheckPeds = false;
            flags.bCheckObjects = false;
            flags.bCheckDummies = false;
            if (std::abs(member.position.fZ - firstGroundPosition.fZ) >= 1.0f || !m_pWorld->IsLineOfSightClear(&member.position, &firstGroundPosition, flags))
            {
                continue;
            }
        }

        candidate.members[candidate.count++] = member;
    }

    return candidate.count >= 2 ? EAmbientPedSpawnCandidateResult::Success : EAmbientPedSpawnCandidateResult::Blocked;
}

EAmbientPedSpawnCandidateResult CGameSA::GetAmbientPedCivilianCoupleCandidate(const CVector& origin, SAmbientPedCivilianCoupleSpawnCandidate& candidate)
{
    candidate = {};
    if (m_eGameVersion != VERSION_US_10 || !std::isfinite(origin.fX) || !std::isfinite(origin.fY) || !std::isfinite(origin.fZ))
        return EAmbientPedSpawnCandidateResult::InvalidOrigin;

    int modelA = -1;
    int modelB = -1;
    reinterpret_cast<void(__cdecl*)(int&, int&)>(0x613180)(modelA, modelB);
    if (modelA < 0 || modelB < 0 || modelA == modelB)
        return EAmbientPedSpawnCandidateResult::NoModel;

    auto* modelInfoA = reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(modelA));
    auto* modelInfoB = reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(modelB));
    if (!modelInfoA || !modelInfoB || !modelInfoA->pRwObject || !modelInfoB->pRwObject)
        return EAmbientPedSpawnCandidateResult::UnsupportedModel;

    // Reuse the audited native path/visibility producer, then replace only its
    // throwaway singleton occupation with the two occupations selected above.
    // The extra singleton selection is deliberately not authoritative and is
    // never exposed to the server.
    SAmbientPedSpawnCandidate anchor;
    const auto                result = GetAmbientPedSpawnCandidateForPopulation(origin, EAmbientPedPopulationSelection::Civilian, 0xFF, anchor);
    if (result != EAmbientPedSpawnCandidateResult::Success)
        return result;

    candidate.members[0] = anchor;
    candidate.members[0].modelId = static_cast<unsigned int>(modelA);
    candidate.members[0].pedType = static_cast<unsigned char>(modelInfoA->pedType);
    candidate.members[0].populationClass = EAmbientPedPopulationClass::Civilian;
    candidate.members[0].gangId = 0xFF;
    candidate.members[0].headingDegrees = 0.0f;

    candidate.members[1] = anchor;
    candidate.members[1].modelId = static_cast<unsigned int>(modelB);
    candidate.members[1].pedType = static_cast<unsigned char>(modelInfoB->pedType);
    candidate.members[1].populationClass = EAmbientPedPopulationClass::Civilian;
    candidate.members[1].gangId = 0xFF;
    candidate.members[1].position.fX += 1.0f;
    candidate.members[1].wanderDirection = static_cast<unsigned char>(rand() & 7);
    candidate.members[1].headingDegrees = 0.0f;

    for (const auto& member : candidate.members)
    {
        auto*       modelInfo = reinterpret_cast<CPedModelInfoSAInterface*>(CModelInfoSAInterface::GetModelInfo(member.modelId));
        const float boundRadius = modelInfo && modelInfo->pColModel ? modelInfo->pColModel->m_sphere.m_radius : -1.0f;
        if (!reinterpret_cast<bool(__cdecl*)(const CVector&, float, int, void*, bool, bool, bool)>(FUNC_IsPositionClearForPed)(member.position, boundRadius, -1,
                                                                                                                               nullptr, true, true, true))
        {
            candidate = {};
            return EAmbientPedSpawnCandidateResult::Blocked;
        }
    }
    return EAmbientPedSpawnCandidateResult::Success;
}

bool CGameSA::AcquireAmbientPedNativeGroup(CPed* const* members, unsigned char count, unsigned int& nativeGroupId)
{
    nativeGroupId = std::numeric_limits<unsigned int>::max();
    if (!members || count < 2 || count > AMBIENT_PED_GROUP_MAX_MEMBERS)
        return false;

    std::array<CPedSA*, AMBIENT_PED_GROUP_MAX_MEMBERS> peds{};
    for (unsigned char index = 0; index < count; ++index)
    {
        peds[index] = dynamic_cast<CPedSA*>(members[index]);
        if (!peds[index] || !peds[index]->GetPedInterface() || peds[index]->IsNativeAmbientGroupActive())
            return false;
        for (unsigned char prior = 0; prior < index; ++prior)
        {
            if (peds[prior] == peds[index])
                return false;
        }
        if (reinterpret_cast<void*(__cdecl*)(CPedSAInterface*)>(FUNC_GetPedsGroup)(peds[index]->GetPedInterface()))
            return false;
    }

    if (m_ambientPedNativeGroupLeases.size() >= MAX_AMBIENT_PED_NATIVE_GROUPS)
        return false;

    const int groupId = AcquireNonPlayerPedGroupSlot();
    if (groupId <= 0 || static_cast<unsigned int>(groupId) >= PED_GROUP_COUNT)
        return false;
    if (m_ambientPedNativeGroupLeases.contains(groupId))
    {
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_RemovePedGroup)(groupId);
        return false;
    }

    auto* membership = GetPedGroupMembershipInterface(groupId);
    auto* intelligence = GetPedGroupIntelligenceInterface(groupId);
    if (!membership || !intelligence)
    {
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_RemovePedGroup)(groupId);
        return false;
    }

    SAmbientPedNativeGroupLease lease;
    lease.count = count;
    for (unsigned char index = 0; index < count; ++index)
        lease.members[index] = peds[index];
    const auto [leaseIter, inserted] = m_ambientPedNativeGroupLeases.emplace(groupId, lease);
    if (!inserted)
    {
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_RemovePedGroup)(groupId);
        return false;
    }

    for (auto* ped : peds)
    {
        if (ped)
            ped->SetNativeAmbientGroupActive(true);
    }

    reinterpret_cast<void(__thiscall*)(void*, int)>(FUNC_SetPedGroupDefaultTaskAllocatorType)(intelligence, PED_GROUP_RANDOM_TASK_ALLOCATOR);
    for (unsigned char index = 0; index < count; ++index)
    {
        const int memberIndex = index == 0 ? PED_GROUP_LEADER_MEMBER_INDEX : index - 1;
        reinterpret_cast<void(__thiscall*)(void*, CPedSAInterface*, int)>(FUNC_AddPedGroupMember)(membership, peds[index]->GetPedInterface(), memberIndex);
        reinterpret_cast<void(__thiscall*)(void*)>(FUNC_ProcessPedGroupMembership)(membership);
        reinterpret_cast<void(__thiscall*)(void*)>(FUNC_ProcessPedGroupIntelligence)(intelligence);

        auto* wander = new CTaskComplexWanderGangSA(PedMoveState::PEDMOVE_WALK, static_cast<unsigned char>(rand() & 7), 5000, true, 0.5f);
        auto* beInGroup = new CTaskComplexBeInGroupSA(groupId, false);
        if (!wander->IsValid() || !beInGroup->IsValid())
        {
            delete wander;
            delete beInGroup;
            ReleaseAmbientPedNativeGroup(groupId, members, count);
            return false;
        }
        m_pTaskManagementSystem->AddTask(wander);
        m_pTaskManagementSystem->AddTask(beInGroup);
        auto& nativeLease = leaseIter->second;
        nativeLease.defaultTasks[index] = wander->GetInterface();
        nativeLease.primaryTasks[index] = beInGroup->GetInterface();
        auto* taskManager = static_cast<CTaskManagerSA*>(peds[index]->GetPedIntelligence()->GetTaskManager());
        taskManager->SetTask(wander, TASK_PRIORITY_DEFAULT, true);
        taskManager->SetTask(beInGroup, TASK_PRIORITY_PRIMARY, true);
    }

    nativeGroupId = static_cast<unsigned int>(groupId);
    return true;
}

bool CGameSA::ReleaseAmbientPedNativeGroup(unsigned int nativeGroupId, CPed* const* members, unsigned char count)
{
    const auto leaseIter = m_ambientPedNativeGroupLeases.find(nativeGroupId);
    if (!members || leaseIter == m_ambientPedNativeGroupLeases.end() || count != leaseIter->second.count)
        return false;

    auto* group = GetPedGroupInterface(nativeGroupId);
    bool  ownsActiveSlot = false;
    for (unsigned char index = 0; index < count; ++index)
    {
        auto* ped = dynamic_cast<CPedSA*>(members[index]);
        if (!ped || ped != leaseIter->second.members[index] || !ped->GetPedInterface())
            continue;
        ownsActiveSlot = ownsActiveSlot || (ped->IsNativeAmbientGroupActive() &&
                                            reinterpret_cast<void*(__cdecl*)(CPedSAInterface*)>(FUNC_GetPedsGroup)(ped->GetPedInterface()) == group);
        auto*  taskManager = static_cast<CTaskManagerSA*>(ped->GetPedIntelligence()->GetTaskManager());
        CTask* primaryTask = taskManager->GetTask(TASK_PRIORITY_PRIMARY);
        if (primaryTask && primaryTask->GetInterface() == leaseIter->second.primaryTasks[index])
            taskManager->SetTask(nullptr, TASK_PRIORITY_PRIMARY, true);

        CTask* defaultTask = taskManager->GetTask(TASK_PRIORITY_DEFAULT);
        if (defaultTask && defaultTask->GetInterface() == leaseIter->second.defaultTasks[index])
        {
            auto* replacement = dynamic_cast<CTaskSimplePlayerOnFootSA*>(m_pTasks->CreateTaskSimplePlayerOnFoot());
            if (replacement && replacement->IsValid())
                taskManager->SetTask(replacement, TASK_PRIORITY_DEFAULT, true);
        }
        ped->SetNativeAmbientGroupActive(false);
    }
    // The eight GTA slots have no generation counter. If every tracked member
    // has already vanished, CPedGroups::Process owns empty-slot cleanup; never
    // risk deleting a mission/resource group which reused the same index.
    if (IsPedGroupSlotActive(nativeGroupId) && ownsActiveSlot)
        reinterpret_cast<void(__cdecl*)(int)>(FUNC_RemovePedGroup)(nativeGroupId);
    m_ambientPedNativeGroupLeases.erase(leaseIter);
    return true;
}

bool CGameSA::IsAmbientPedNativeGroupActive(unsigned int nativeGroupId, CPed* const* members, unsigned char count) const
{
    const auto leaseIter = m_ambientPedNativeGroupLeases.find(nativeGroupId);
    if (!members || leaseIter == m_ambientPedNativeGroupLeases.end() || count != leaseIter->second.count || !IsPedGroupSlotActive(nativeGroupId))
        return false;
    auto* group = GetPedGroupInterface(nativeGroupId);
    bool  hasTrackedMember = false;
    for (unsigned char index = 0; index < count; ++index)
    {
        auto* ped = dynamic_cast<CPedSA*>(members[index]);
        hasTrackedMember = hasTrackedMember || (ped && ped->IsNativeAmbientGroupActive() && ped->GetPedInterface() &&
                                                reinterpret_cast<void*(__cdecl*)(CPedSAInterface*)>(FUNC_GetPedsGroup)(ped->GetPedInterface()) == group);
    }
    return hasTrackedMember;
}

void CGameSA::GetAmbientPedNativeGroupDiagnostic(unsigned int nativeGroupId, CPed* const* members, unsigned char count,
                                                 SAmbientPedNativeGroupDiagnostic& diagnostic) const
{
    diagnostic.nativeGroupId = nativeGroupId;
    diagnostic.memberCount = count;

    const auto leaseIter = m_ambientPedNativeGroupLeases.find(nativeGroupId);
    diagnostic.gameLeasePresent = leaseIter != m_ambientPedNativeGroupLeases.end();
    if (!diagnostic.gameLeasePresent)
        return;

    diagnostic.memberCountMatches = count == leaseIter->second.count;
    diagnostic.slotActive = IsPedGroupSlotActive(nativeGroupId);
    auto*               group = GetPedGroupInterface(nativeGroupId);
    const unsigned char inspectedCount = std::min<unsigned char>(count, AMBIENT_PED_GROUP_MAX_MEMBERS);
    for (unsigned char index = 0; index < inspectedCount; ++index)
    {
        auto& member = diagnostic.members[index];
        auto* ped = members ? dynamic_cast<CPedSA*>(members[index]) : nullptr;
        member.leaseMemberMatches = ped && ped == leaseIter->second.members[index];
        member.expectedGamePedAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(leaseIter->second.members[index]));
        member.expectedPrimaryTaskAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(leaseIter->second.primaryTasks[index]));
        member.expectedDefaultTaskAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(leaseIter->second.defaultTasks[index]));
        if (!ped || !ped->GetPedInterface())
            continue;

        member.nativeAmbientGroupFlag = ped->IsNativeAmbientGroupActive();
        member.attachedToExpectedGroup = reinterpret_cast<void*(__cdecl*)(CPedSAInterface*)>(FUNC_GetPedsGroup)(ped->GetPedInterface()) == group;
        diagnostic.hasTrackedMember = diagnostic.hasTrackedMember || (member.nativeAmbientGroupFlag && member.attachedToExpectedGroup);

        auto* taskManager = static_cast<CTaskManagerSA*>(ped->GetPedIntelligence()->GetTaskManager());
        auto* taskManagerInterface = taskManager ? taskManager->GetInterface() : nullptr;
        if (!taskManagerInterface)
            continue;

        member.primaryTaskAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(taskManagerInterface->m_tasks[TASK_PRIORITY_PRIMARY]));
        member.defaultTaskAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(taskManagerInterface->m_tasks[TASK_PRIORITY_DEFAULT]));
        member.primaryTaskType = GetPedTaskType(taskManagerInterface->m_tasks[TASK_PRIORITY_PRIMARY]);
        member.defaultTaskType = GetPedTaskType(taskManagerInterface->m_tasks[TASK_PRIORITY_DEFAULT]);
    }
}

bool CGameSA::ValidateAmbientPedCivilianCouple(CPed* a, CPed* b, SAmbientPedNativeCoupleValidation& validation) const
{
    validation = {};
    if (m_eGameVersion != VERSION_US_10 || !a || !b || a == b)
        return false;

    auto* pedA = dynamic_cast<CPedSA*>(a);
    auto* pedB = dynamic_cast<CPedSA*>(b);
    if (!pedA || !pedB || !pedA->GetPedInterface() || !pedB->GetPedInterface() || pedA->IsNativeAmbientGroupActive() || pedB->IsNativeAmbientGroupActive())
    {
        return false;
    }

    using GetWalkAnimSpeed = float(__thiscall*)(CPedSAInterface*);
    validation.walkSpeedA = reinterpret_cast<GetWalkAnimSpeed>(0x5E04B0)(pedA->GetPedInterface());
    validation.walkSpeedB = reinterpret_cast<GetWalkAnimSpeed>(0x5E04B0)(pedB->GetPedInterface());
    if (!std::isfinite(validation.walkSpeedA) || !std::isfinite(validation.walkSpeedB))
        return false;

    validation.compatible =
        validation.walkSpeedA >= 0.75f && validation.walkSpeedB >= 0.75f && std::abs(validation.walkSpeedA - validation.walkSpeedB) <= 0.45f;
    // Retail makes B leader only when B is strictly faster. A wins an exact
    // tie, which matters when both actors share the same animation group.
    validation.aLeader = !(validation.walkSpeedB > validation.walkSpeedA);
    return true;
}

bool CGameSA::AcquireAmbientPedCivilianCouple(CPed* a, CPed* b, bool aLeader, unsigned int& nativeCoupleId)
{
    nativeCoupleId = 0;
    SAmbientPedNativeCoupleValidation validation;
    if (!ValidateAmbientPedCivilianCouple(a, b, validation) || !validation.compatible || validation.aLeader != aLeader)
        return false;

    auto* pedA = dynamic_cast<CPedSA*>(a);
    auto* pedB = dynamic_cast<CPedSA*>(b);
    for (const auto& [id, lease] : m_ambientPedNativeCoupleLeases)
    {
        if (lease.members[0] == a || lease.members[1] == a || lease.members[0] == b || lease.members[1] == b)
            return false;
    }
    for (const auto& [id, lease] : m_ambientPedNativeCouplePresentationLeases)
    {
        if (lease.members[0] == a || lease.members[1] == a || lease.members[0] == b || lease.members[1] == b)
            return false;
    }

    auto* taskA = new CTaskComplexBeInCoupleSA(pedB, aLeader);
    auto* taskB = new CTaskComplexBeInCoupleSA(pedA, !aLeader);
    auto* taskManagerA = static_cast<CTaskManagerSA*>(pedA->GetPedIntelligence()->GetTaskManager());
    auto* taskManagerB = static_cast<CTaskManagerSA*>(pedB->GetPedIntelligence()->GetTaskManager());
    if (!taskA->IsValid() || !taskB->IsValid() || !taskManagerA || !taskManagerB)
    {
        delete taskA;
        delete taskB;
        return false;
    }

    unsigned int leaseId = 0;
    do
    {
        leaseId = m_nextAmbientPedNativeCoupleId++;
    } while (leaseId == 0 || m_ambientPedNativeCoupleLeases.contains(leaseId));

    SAmbientPedNativeCoupleLease lease;
    lease.members = {a, b};
    lease.primaryTasks = {taskA->GetInterface(), taskB->GetInterface()};
    lease.aLeader = aLeader;
    const auto [leaseIter, inserted] = m_ambientPedNativeCoupleLeases.emplace(leaseId, lease);
    if (!inserted)
    {
        delete taskA;
        delete taskB;
        return false;
    }

    m_pTaskManagementSystem->AddTask(taskA);
    m_pTaskManagementSystem->AddTask(taskB);
    taskManagerA->SetTask(taskA, TASK_PRIORITY_PRIMARY, true);
    taskManagerB->SetTask(taskB, TASK_PRIORITY_PRIMARY, true);
    const CTask* installedA = taskManagerA->GetTask(TASK_PRIORITY_PRIMARY);
    const CTask* installedB = taskManagerB->GetTask(TASK_PRIORITY_PRIMARY);
    if (!installedA || !installedB || installedA->GetInterface() != leaseIter->second.primaryTasks[0] ||
        installedB->GetInterface() != leaseIter->second.primaryTasks[1])
    {
        ReleaseAmbientPedCivilianCouple(leaseId, a, b);
        return false;
    }

    nativeCoupleId = leaseId;
    return true;
}

bool CGameSA::ReleaseAmbientPedCivilianCouple(unsigned int nativeCoupleId, CPed* a, CPed* b)
{
    const auto leaseIter = m_ambientPedNativeCoupleLeases.find(nativeCoupleId);
    if (leaseIter == m_ambientPedNativeCoupleLeases.end() || (a && a != leaseIter->second.members[0]) || (b && b != leaseIter->second.members[1]))
        return false;

    const std::array<CPed*, 2> members = {a, b};
    for (std::size_t index = 0; index < members.size(); ++index)
    {
        auto* ped = dynamic_cast<CPedSA*>(members[index]);
        if (!ped || !ped->GetPedInterface())
            continue;
        auto* taskManager = static_cast<CTaskManagerSA*>(ped->GetPedIntelligence()->GetTaskManager());
        if (!taskManager)
            continue;
        CTask* primaryTask = taskManager->GetTask(TASK_PRIORITY_PRIMARY);
        if (primaryTask && primaryTask->GetInterface() == leaseIter->second.primaryTasks[index])
        {
            taskManager->SetTask(nullptr, TASK_PRIORITY_PRIMARY, true);
            RemoveAmbientCoupleArmIK(taskManager);
        }
    }
    m_ambientPedNativeCoupleLeases.erase(leaseIter);
    return true;
}

bool CGameSA::IsAmbientPedCivilianCoupleActive(unsigned int nativeCoupleId, CPed* a, CPed* b) const
{
    SAmbientPedNativeCoupleDiagnostic diagnostic;
    GetAmbientPedCivilianCoupleDiagnostic(nativeCoupleId, a, b, diagnostic);
    return diagnostic.active;
}

void CGameSA::RecordAmbientPedCivilianCoupleForwardedEvent(void* eventGroup, int eventType)
{
    std::size_t eventIndex;
    switch (eventType)
    {
        case 9:
            eventIndex = 0;
            break;
        case 15:
            eventIndex = 1;
            break;
        case 31:
            eventIndex = 2;
            break;
        default:
            return;
    }

    for (auto& [id, lease] : m_ambientPedNativeCoupleLeases)
    {
        for (std::size_t memberIndex = 0; memberIndex < lease.members.size(); ++memberIndex)
        {
            auto* ped = dynamic_cast<CPedSA*>(lease.members[memberIndex]);
            auto* intelligence = ped && ped->GetPedInterface() ? ped->GetPedInterface()->pPedIntelligence : nullptr;
            if (intelligence && intelligence->eventGroup == eventGroup)
            {
                ++lease.forwardedEventCounts[memberIndex][eventIndex];
                return;
            }
        }
    }
}

void CGameSA::GetAmbientPedCivilianCoupleDiagnostic(unsigned int nativeCoupleId, CPed* a, CPed* b, SAmbientPedNativeCoupleDiagnostic& diagnostic) const
{
    diagnostic.nativeCoupleId = nativeCoupleId;
    const auto leaseIter = m_ambientPedNativeCoupleLeases.find(nativeCoupleId);
    diagnostic.gameLeasePresent = leaseIter != m_ambientPedNativeCoupleLeases.end();
    if (!diagnostic.gameLeasePresent)
        return;

    diagnostic.aLeader = leaseIter->second.aLeader;
    const std::array<CPed*, 2> members = {a, b};
    bool                       allMembersMatch = true;
    bool                       allPrimaryTasksMatch = true;
    bool                       allPartnersMatch = true;
    bool                       allRolesMatch = true;
    for (std::size_t index = 0; index < members.size(); ++index)
    {
        auto& member = diagnostic.members[index];
        member.forwardedDamageEventCount = leaseIter->second.forwardedEventCounts[index][0];
        member.forwardedShotFiredEventCount = leaseIter->second.forwardedEventCounts[index][1];
        member.forwardedGunAimedAtEventCount = leaseIter->second.forwardedEventCounts[index][2];
        auto* ped = dynamic_cast<CPedSA*>(members[index]);
        auto* partnerPed = dynamic_cast<CPedSA*>(members[index == 0 ? 1 : 0]);
        auto* expectedPartnerPed = dynamic_cast<CPedSA*>(leaseIter->second.members[index == 0 ? 1 : 0]);
        member.leaseMemberMatches = ped && ped == leaseIter->second.members[index];
        member.expectedGamePedAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(leaseIter->second.members[index]));
        member.expectedPartnerAddress =
            expectedPartnerPed ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(expectedPartnerPed->GetPedInterface())) : 0;
        member.expectedPrimaryTaskAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(leaseIter->second.primaryTasks[index]));
        allMembersMatch = allMembersMatch && member.leaseMemberMatches;
        if (!ped || !ped->GetPedInterface())
        {
            allPrimaryTasksMatch = false;
            allPartnersMatch = false;
            allRolesMatch = false;
            continue;
        }

        member.gamePedAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ped));
        member.currentEventType = ped->GetNativeCurrentEventType();
        if (auto* intelligence = ped->GetPedInterface()->pPedIntelligence)
        {
            using GetEventOfType = void*(__thiscall*)(void*, int);
            auto* eventGroup = intelligence->eventGroup;
            member.damageEventPresent = reinterpret_cast<GetEventOfType>(0x4AB650)(eventGroup, 9) != nullptr;
            member.shotFiredEventPresent = reinterpret_cast<GetEventOfType>(0x4AB650)(eventGroup, 15) != nullptr;
            member.gunAimedAtEventPresent = reinterpret_cast<GetEventOfType>(0x4AB650)(eventGroup, 31) != nullptr;
        }
        using GetWalkAnimSpeed = float(__thiscall*)(CPedSAInterface*);
        member.walkSpeed = reinterpret_cast<GetWalkAnimSpeed>(0x5E04B0)(ped->GetPedInterface());
        auto* taskManager = static_cast<CTaskManagerSA*>(ped->GetPedIntelligence()->GetTaskManager());
        auto* taskManagerInterface = taskManager ? taskManager->GetInterface() : nullptr;
        auto* primaryTask = taskManagerInterface ? taskManagerInterface->m_tasks[TASK_PRIORITY_PRIMARY] : nullptr;
        member.primaryTaskAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(primaryTask));
        member.primaryTaskType = GetPedTaskType(primaryTask);
        member.primaryTaskMatchesLease = primaryTask && primaryTask == leaseIter->second.primaryTasks[index];
        allPrimaryTasksMatch = allPrimaryTasksMatch && member.primaryTaskMatchesLease;
        if (!primaryTask || member.primaryTaskType != TASK_COMPLEX_BE_IN_COUPLE)
        {
            allPartnersMatch = false;
            allRolesMatch = false;
            continue;
        }

        const auto* coupleTask = static_cast<const CTaskComplexBeInCoupleSAInterface*>(primaryTask);
        member.partnerAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(coupleTask->m_partner));
        member.reciprocalPartner = partnerPed && coupleTask->m_partner == partnerPed->GetPedInterface();
        member.leaderRoleMatches = coupleTask->m_isLeader == (index == 0 ? leaseIter->second.aLeader : !leaseIter->second.aLeader);
        member.subTaskType = GetPedTaskType(reinterpret_cast<CTaskSAInterface*>(coupleTask->m_pSubTask));
        member.previousSide = coupleTask->m_previousSide;
        allPartnersMatch = allPartnersMatch && member.reciprocalPartner;
        allRolesMatch = allRolesMatch && member.leaderRoleMatches;
    }

    if (!allMembersMatch)
        diagnostic.status = EAmbientPedNativeCoupleDiagnosticStatus::LeaseMemberMismatch;
    else if (!diagnostic.members[0].primaryTaskAddress || !diagnostic.members[1].primaryTaskAddress)
        diagnostic.status = EAmbientPedNativeCoupleDiagnosticStatus::PrimaryTaskMissing;
    else if (!allPrimaryTasksMatch)
        diagnostic.status = EAmbientPedNativeCoupleDiagnosticStatus::PrimaryTaskReplaced;
    else if (!allPartnersMatch)
        diagnostic.status = EAmbientPedNativeCoupleDiagnosticStatus::PartnerMismatch;
    else if (!allRolesMatch)
        diagnostic.status = EAmbientPedNativeCoupleDiagnosticStatus::RoleMismatch;
    else
    {
        diagnostic.status = EAmbientPedNativeCoupleDiagnosticStatus::Active;
        diagnostic.active = true;
    }
}

bool CGameSA::AcquireAmbientPedCivilianCouplePresentation(CPed* a, CPed* b, unsigned int& nativePresentationId)
{
    LogAmbientCouplePresentationAbiOnce();
    nativePresentationId = 0;
    if (m_eGameVersion != VERSION_US_10)
    {
        LogAmbientCouplePresentationAcquireReject("wrong-version", m_ambientPedNativeCoupleLeases.size(), m_ambientPedNativeCouplePresentationLeases.size());
        return false;
    }
    if (!a)
    {
        LogAmbientCouplePresentationAcquireReject("null-a", m_ambientPedNativeCoupleLeases.size(), m_ambientPedNativeCouplePresentationLeases.size());
        return false;
    }
    if (!b)
    {
        LogAmbientCouplePresentationAcquireReject("null-b", m_ambientPedNativeCoupleLeases.size(), m_ambientPedNativeCouplePresentationLeases.size());
        return false;
    }
    if (a == b)
    {
        LogAmbientCouplePresentationAcquireReject("same-ped", m_ambientPedNativeCoupleLeases.size(), m_ambientPedNativeCouplePresentationLeases.size());
        return false;
    }

    auto* pedA = dynamic_cast<CPedSA*>(a);
    auto* pedB = dynamic_cast<CPedSA*>(b);
    if (!pedA)
    {
        LogAmbientCouplePresentationAcquireReject("cast-a", m_ambientPedNativeCoupleLeases.size(), m_ambientPedNativeCouplePresentationLeases.size());
        return false;
    }
    if (!pedB)
    {
        LogAmbientCouplePresentationAcquireReject("cast-b", m_ambientPedNativeCoupleLeases.size(), m_ambientPedNativeCouplePresentationLeases.size());
        return false;
    }
    if (!pedA->GetPedInterface())
    {
        LogAmbientCouplePresentationAcquireReject("no-interface-a", m_ambientPedNativeCoupleLeases.size(), m_ambientPedNativeCouplePresentationLeases.size());
        return false;
    }
    if (!pedB->GetPedInterface())
    {
        LogAmbientCouplePresentationAcquireReject("no-interface-b", m_ambientPedNativeCoupleLeases.size(), m_ambientPedNativeCouplePresentationLeases.size());
        return false;
    }

    for (const auto& [id, lease] : m_ambientPedNativeCoupleLeases)
    {
        if (lease.members[0] == a || lease.members[1] == a)
        {
            LogAmbientCouplePresentationAcquireReject("native-lease-conflict-a", m_ambientPedNativeCoupleLeases.size(),
                                                      m_ambientPedNativeCouplePresentationLeases.size(), id, lease.members[0] == a ? 0 : 1);
            return false;
        }
        if (lease.members[0] == b || lease.members[1] == b)
        {
            LogAmbientCouplePresentationAcquireReject("native-lease-conflict-b", m_ambientPedNativeCoupleLeases.size(),
                                                      m_ambientPedNativeCouplePresentationLeases.size(), id, lease.members[0] == b ? 0 : 1);
            return false;
        }
    }
    for (const auto& [id, lease] : m_ambientPedNativeCouplePresentationLeases)
    {
        if (lease.members[0] == a || lease.members[1] == a)
        {
            LogAmbientCouplePresentationAcquireReject("presentation-lease-conflict-a", m_ambientPedNativeCoupleLeases.size(),
                                                      m_ambientPedNativeCouplePresentationLeases.size(), id, lease.members[0] == a ? 0 : 1);
            return false;
        }
        if (lease.members[0] == b || lease.members[1] == b)
        {
            LogAmbientCouplePresentationAcquireReject("presentation-lease-conflict-b", m_ambientPedNativeCoupleLeases.size(),
                                                      m_ambientPedNativeCouplePresentationLeases.size(), id, lease.members[0] == b ? 0 : 1);
            return false;
        }
    }

    unsigned int leaseId = 0;
    do
    {
        leaseId = m_nextAmbientPedNativeCouplePresentationId++;
    } while (leaseId == 0 || m_ambientPedNativeCouplePresentationLeases.contains(leaseId));

    SAmbientPedNativeCouplePresentationLease lease;
    lease.members = {a, b};
    if (!m_ambientPedNativeCouplePresentationLeases.emplace(leaseId, lease).second)
    {
        LogAmbientCouplePresentationAcquireReject("lease-insert-failed", m_ambientPedNativeCoupleLeases.size(),
                                                  m_ambientPedNativeCouplePresentationLeases.size());
        return false;
    }

    nativePresentationId = leaseId;
    if (g_pCore)
    {
        g_pCore->GetConsole()->Printf("[couple-presentation][acquired] module=game_sa nativeLease=%u nativeLeases=%u presentationLeases=%u", leaseId,
                                      static_cast<unsigned int>(m_ambientPedNativeCoupleLeases.size()),
                                      static_cast<unsigned int>(m_ambientPedNativeCouplePresentationLeases.size()));
    }
    return true;
}

bool CGameSA::UpdateAmbientPedCivilianCouplePresentation(unsigned int nativePresentationId, CPed* a, CPed* b)
{
    return UpdateAmbientPedCivilianCouplePresentationWithSides(nativePresentationId, a, b, 0, 0);
}

bool CGameSA::UpdateAmbientPedCivilianCouplePresentationWithSides(unsigned int nativePresentationId, CPed* a, CPed* b, unsigned char sideA, unsigned char sideB)
{
    const auto leaseIter = m_ambientPedNativeCouplePresentationLeases.find(nativePresentationId);
    if (leaseIter == m_ambientPedNativeCouplePresentationLeases.end() || a != leaseIter->second.members[0] || b != leaseIter->second.members[1])
        return false;

    auto* pedA = dynamic_cast<CPedSA*>(a);
    auto* pedB = dynamic_cast<CPedSA*>(b);
    if (!pedA || !pedB || !pedA->GetPedInterface() || !pedB->GetPedInterface())
        return false;

    const CVector* positionA = pedA->GetPosition();
    const CVector* positionB = pedB->GetPosition();
    if (!positionA || !positionB)
        return true;

    const float dx = positionB->fX - positionA->fX;
    const float dy = positionB->fY - positionA->fY;
    const float horizontalDistanceSquared = dx * dx + dy * dy;
    // Retail CTaskComplexBeInCouple only creates the hand IK while the two
    // actors are strictly less than 1.5 metres apart in XY.
    if (!std::isfinite(horizontalDistanceSquared) || horizontalDistanceSquared >= 2.25f)
    {
        DestroyAmbientCouplePointArm(leaseIter->second.pointArmTasks[0]);
        DestroyAmbientCouplePointArm(leaseIter->second.pointArmTasks[1]);
        leaseIter->second.activeArms = {};
        return true;
    }

    // Retail aims each arm at the shared XY midpoint while retaining the
    // owning ped's Z. A shared midpoint Z bends one observer arm differently
    // whenever the synced actors are even slightly vertically offset.
    const float                           midpointX = positionA->fX + dx * 0.5f;
    const float                           midpointY = positionA->fY + dy * 0.5f;
    const std::array<CVector, 2>          targets = {CVector(midpointX, midpointY, positionA->fZ), CVector(midpointX, midpointY, positionB->fZ)};
    const std::array<CPedSAInterface*, 2> peds = {pedA->GetPedInterface(), pedB->GetPedInterface()};
    const std::array<unsigned char, 2>    ownerSides = {sideA, sideB};

    for (std::size_t index = 0; index < peds.size(); ++index)
    {
        // previousSide is remembered by the retail task even when its IK is
        // inactive. Wait for the authoritative owner value instead of calling
        // GTA's side picker on an observer ped whose partner matrix may not yet
        // exist after streaming or handoff.
        const int arm = ownerSides[index] == 2 ? 0 : ownerSides[index] == 1 ? 1 : -1;
        if (arm < 0)
        {
            DestroyAmbientCouplePointArm(leaseIter->second.pointArmTasks[index]);
            leaseIter->second.activeArms[index] = 0;
            continue;
        }

        const unsigned char encodedArm = static_cast<unsigned char>(arm + 1);
        if (leaseIter->second.activeArms[index] != 0 && leaseIter->second.activeArms[index] != encodedArm)
        {
            // GTA removes the old arm and creates the replacement in the same
            // frame; PointArm itself owns the native 250 ms blend.
            DestroyAmbientCouplePointArm(leaseIter->second.pointArmTasks[index]);
            leaseIter->second.activeArms[index] = 0;
        }

        leaseIter->second.activeArms[index] =
            UpdateAmbientCouplePointArm(leaseIter->second.pointArmTasks[index], peds[index], arm, targets[index]) ? encodedArm : 0;
    }
    return true;
}

bool CGameSA::ReleaseAmbientPedCivilianCouplePresentation(unsigned int nativePresentationId, CPed* a, CPed* b)
{
    const auto leaseIter = m_ambientPedNativeCouplePresentationLeases.find(nativePresentationId);
    if (leaseIter == m_ambientPedNativeCouplePresentationLeases.end() || (a && a != leaseIter->second.members[0]) || (b && b != leaseIter->second.members[1]))
    {
        return false;
    }

    DestroyAmbientCouplePointArm(leaseIter->second.pointArmTasks[0]);
    DestroyAmbientCouplePointArm(leaseIter->second.pointArmTasks[1]);
    m_ambientPedNativeCouplePresentationLeases.erase(leaseIter);
    return true;
}

bool CGameSA::IsAmbientPedCivilianCouplePresentationActive(unsigned int nativePresentationId, CPed* a, CPed* b) const
{
    const auto leaseIter = m_ambientPedNativeCouplePresentationLeases.find(nativePresentationId);
    return leaseIter != m_ambientPedNativeCouplePresentationLeases.end() && a == leaseIter->second.members[0] && b == leaseIter->second.members[1] &&
           leaseIter->second.activeArms[0] != 0 && leaseIter->second.activeArms[1] != 0;
}

eGameVersion CGameSA::FindGameVersion()
{
    unsigned char ucA = *reinterpret_cast<unsigned char*>(0x748ADD);
    unsigned char ucB = *reinterpret_cast<unsigned char*>(0x748ADE);
    if (ucA == 0xFF && ucB == 0x53)
    {
        m_eGameVersion = VERSION_US_10;
    }
    else if (ucA == 0x0F && ucB == 0x84)
    {
        m_eGameVersion = VERSION_EU_10;
    }
    else if (ucA == 0xFE && ucB == 0x10)
    {
        m_eGameVersion = VERSION_11;
    }
    else
    {
        m_eGameVersion = VERSION_UNKNOWN;
    }

    return m_eGameVersion;
}

float CGameSA::GetFPS()
{
    return *(float*)0xB7CB50;  // CTimer::game_FPS
}

float CGameSA::GetTimeStep()
{
    return *(float*)0xB7CB5C;  // CTimer::ms_fTimeStep
}

float CGameSA::GetOldTimeStep()
{
    return *(float*)0xB7CB54;  // CTimer::ms_fOldTimeStep
}

float CGameSA::GetTimeScale()
{
    return *(float*)0xB7CB64;  // CTimer::ms_fTimeScale
}

void CGameSA::SetTimeScale(float fTimeScale)
{
    MemPutFast<float>(0xB7CB64, fTimeScale);  // CTimer::ms_fTimeScale
}

unsigned char CGameSA::GetBlurLevel()
{
    return *(unsigned char*)0x8D5104;  // CPostEffects::m_SpeedFXAlpha
}

void CGameSA::SetBlurLevel(unsigned char ucLevel)
{
    MemPutFast<unsigned char>(0x8D5104, ucLevel);  // CPostEffects::m_SpeedFXAlpha
}

unsigned long CGameSA::GetMinuteDuration()
{
    return *(unsigned long*)0xB7015C;  // CClock::ms_nMillisecondsPerGameMinute
}

void CGameSA::SetMinuteDuration(unsigned long ulTime)
{
    MemPutFast<unsigned long>(0xB7015C, ulTime);  // CClock::ms_nMillisecondsPerGameMinute
}

bool CGameSA::IsCheatEnabled(const char* szCheatName)
{
    std::map<std::string, SCheatSA*>::iterator it = m_Cheats.find(szCheatName);
    if (it == m_Cheats.end())
        return false;
    return *(it->second->m_byAddress) != 0;
}

bool CGameSA::SetCheatEnabled(const char* szCheatName, bool bEnable)
{
    std::map<std::string, SCheatSA*>::iterator it = m_Cheats.find(szCheatName);
    if (it == m_Cheats.end())
        return false;
    if (!it->second->m_bCanBeSet)
        return false;
    MemPutFast<BYTE>(it->second->m_byAddress, bEnable);
    it->second->m_bEnabled = bEnable;
    return true;
}

void CGameSA::ResetCheats()
{
    std::map<std::string, SCheatSA*>::iterator it;
    for (it = m_Cheats.begin(); it != m_Cheats.end(); it++)
    {
        if (it->second->m_byAddress > (BYTE*)0x8A4000)
            MemPutFast<BYTE>(it->second->m_byAddress, 0);
        else
            MemPut<BYTE>(it->second->m_byAddress, 0);
        it->second->m_bEnabled = false;
    }
}

bool CGameSA::IsRandomFoliageEnabled()
{
    return *(unsigned char*)0x5DD01B == 0x74;
}

void CGameSA::SetRandomFoliageEnabled(bool bEnabled)
{
    // 0xEB skip random foliage generation
    MemPut<BYTE>(0x5DD01B, bEnabled ? 0x74 : 0xEB);
    // 0x74 destroy random foliage loaded
    MemPut<BYTE>(0x5DC536, bEnabled ? 0x75 : 0x74);
}

bool CGameSA::IsMoonEasterEggEnabled()
{
    return *(unsigned char*)0x73ABCF == 0x75;
}

void CGameSA::SetMoonEasterEggEnabled(bool bEnable)
{
    // replace JNZ with JMP (short)
    MemPut<BYTE>(0x73ABCF, bEnable ? 0x75 : 0xEB);
}

bool CGameSA::IsExtraAirResistanceEnabled()
{
    return *(unsigned char*)0x72DDD9 == 0x01;
}

void CGameSA::SetExtraAirResistanceEnabled(bool bEnable)
{
    MemPut<BYTE>(0x72DDD9, bEnable ? 0x01 : 0x00);
}

void CGameSA::SetUnderWorldWarpEnabled(bool bEnable)
{
    m_bUnderworldWarp = !bEnable;
}

bool CGameSA::IsUnderWorldWarpEnabled()
{
    return !m_bUnderworldWarp;
}

bool CGameSA::GetJetpackWeaponEnabled(eWeaponType weaponType)
{
    if (weaponType >= WEAPONTYPE_BRASSKNUCKLE && weaponType < WEAPONTYPE_LAST_WEAPONTYPE)
    {
        return m_JetpackWeapons[weaponType];
    }
    return false;
}

void CGameSA::SetJetpackWeaponEnabled(eWeaponType weaponType, bool bEnabled)
{
    if (weaponType >= WEAPONTYPE_BRASSKNUCKLE && weaponType < WEAPONTYPE_LAST_WEAPONTYPE)
    {
        m_JetpackWeapons[weaponType] = bEnabled;
    }
}

void CGameSA::SetVehicleSunGlareEnabled(bool bEnabled)
{
    // State turning will be handled in hooks handler
    CVehicleSA::SetVehiclesSunGlareEnabled(bEnabled);
}

bool CGameSA::IsVehicleSunGlareEnabled()
{
    return CVehicleSA::GetVehiclesSunGlareEnabled();
}

void CGameSA::SetCoronaZTestEnabled(bool isEnabled)
{
    if (m_isCoronaZTestEnabled == isEnabled)
        return;

    if (isEnabled)
    {
        // Enable ZTest (PC)
        MemPut<BYTE>(0x6FB17C + 0, 0xFF);
        MemPut<BYTE>(0x6FB17C + 1, 0x51);
        MemPut<BYTE>(0x6FB17C + 2, 0x20);
    }
    else
    {
        // Disable ZTest (PS2)
        MemSet((void*)0x6FB17C, 0x90, 3);
    }

    m_isCoronaZTestEnabled = isEnabled;
}

void CGameSA::SetWaterCreaturesEnabled(bool isEnabled)
{
    if (isEnabled == m_areWaterCreaturesEnabled)
        return;

    const auto manager = reinterpret_cast<class WaterCreatureManager_c*>(0xC1DF30);
    if (isEnabled)
    {
        unsigned char(__thiscall * Init)(WaterCreatureManager_c*) = reinterpret_cast<decltype(Init)>(0x6E3F90);
        Init(manager);
    }
    else
    {
        void(__thiscall * Exit)(WaterCreatureManager_c*) = reinterpret_cast<decltype(Exit)>(0x6E3FD0);
        Exit(manager);
    }

    m_areWaterCreaturesEnabled = isEnabled;
}

void CGameSA::SetTunnelWeatherBlendEnabled(bool isEnabled)
{
    if (isEnabled == m_isTunnelWeatherBlendEnabled)
        return;
    // CWeather::UpdateInTunnelness
    DWORD functionAddress = 0x72B630;
    if (isEnabled)
    {
        // Restore original bytes: 83 EC 20
        MemPut<BYTE>(functionAddress, 0x83);      // Restore 83
        MemPut<BYTE>(functionAddress + 1, 0xEC);  // Restore EC
        MemPut<BYTE>(functionAddress + 2, 0x20);  // Restore 20
    }
    else
    {
        // Patch CWeather::UpdateInTunnelness               (Found By AlexTMjugador)
        MemPut<BYTE>(functionAddress, 0xC3);      // Write C3 (RET)
        MemPut<BYTE>(functionAddress + 1, 0x90);  // Write 90 (NOP)
        MemPut<BYTE>(functionAddress + 2, 0x90);  // Write 90 (NOP)
    }
    m_isTunnelWeatherBlendEnabled = isEnabled;
}

void CGameSA::SetBurnFlippedCarsEnabled(bool isEnabled)
{
    if (isEnabled == m_isBurnFlippedCarsEnabled)
        return;

    // CAutomobile::VehicleDamage
    if (isEnabled)
    {
        BYTE originalCodes[6] = {0xD9, 0x9E, 0xC0, 0x04, 0x00, 0x00};
        MemCpy((void*)0x6A776B, &originalCodes, 6);
    }
    else
    {
        BYTE newCodes[6] = {0xD8, 0xDD, 0x90, 0x90, 0x90, 0x90};
        MemCpy((void*)0x6A776B, &newCodes, 6);
    }

    // CPlayerInfo::Process
    if (isEnabled)
    {
        BYTE originalCodes[6] = {0xD9, 0x99, 0xC0, 0x04, 0x00, 0x00};
        MemCpy((void*)0x570E7F, &originalCodes, 6);
    }
    else
    {
        BYTE newCodes[6] = {0xD8, 0xDD, 0x90, 0x90, 0x90, 0x90};
        MemCpy((void*)0x570E7F, &newCodes, 6);
    }

    m_isBurnFlippedCarsEnabled = isEnabled;
}

void CGameSA::SetFireballDestructEnabled(bool isEnabled)
{
    if (isEnabled == m_isFireballDestructEnabled)
        return;

    if (isEnabled)
    {
        BYTE originalCodes[7] = {0x81, 0x66, 0x1C, 0x7E, 0xFF, 0xFF, 0xFF};
        MemCpy((void*)0x6CCE45, &originalCodes, 7);  // CPlane::BlowUpCar
        MemCpy((void*)0x6C6E01, &originalCodes, 7);  // CHeli::BlowUpCar
    }
    else
    {
        MemSet((void*)0x6CCE45, 0x90, 7);  // CPlane::BlowUpCar
        MemSet((void*)0x6C6E01, 0x90, 7);  // CHeli::BlowUpCar
    }

    m_isFireballDestructEnabled = isEnabled;
}

void CGameSA::SetExtendedWaterCannonsEnabled(bool isEnabled)
{
    if (isEnabled == m_isExtendedWaterCannonsEnabled)
        return;

    // Allocate memory for new bigger array or use default aCannons array
    void* aCannons = isEnabled ? malloc(MAX_WATER_CANNONS * SIZE_CWaterCannon) : (void*)ARRAY_aCannons;

    int newLimit = isEnabled ? MAX_WATER_CANNONS : NUM_CWaterCannon_DefaultLimit;  // default: 3
    MemSetFast(aCannons, 0, newLimit * SIZE_CWaterCannon);                         // clear aCannons array

    // Get current limit
    int currentLimit = *(int*)NUM_WaterCannon_Limit;

    // Get current aCannons array
    void* currentACannons = *(void**)ARRAY_aCannons_CurrentPtr;

    // Call CWaterCannon destructor
    for (int i = 0; i < currentLimit; i++)
    {
        char* currentCannon = (char*)currentACannons + i * SIZE_CWaterCannon;

        ((void(__thiscall*)(int, void*, bool))FUNC_CAESoundManager_CancelSoundsOwnedByAudioEntity)(
            STRUCT_CAESoundManager, currentCannon + NUM_CWaterCannon_Audio_Offset,
            true);  // CAESoundManager::CancelSoundsOwnedByAudioEntity to prevent random crashes from CAESound::UpdateParameters
        ((void(__thiscall*)(void*))FUNC_CWaterCannon_Destructor)(currentCannon);  // CWaterCannon::~CWaterCannon
    }

    // Call CWaterCannon constructor & CWaterCannon::Init
    for (int i = 0; i < newLimit; ++i)
    {
        char* currentCannon = (char*)aCannons + i * SIZE_CWaterCannon;

        ((void(__thiscall*)(void*))FUNC_CWaterCannon_Constructor)(currentCannon);  // CWaterCannon::CWaterCannon
        ((void(__thiscall*)(void*))FUNC_CWaterCannon_Init)(currentCannon);         // CWaterCannon::Init
    }

    // Patch references to array
    MemPut((void*)0x728C83, aCannons);      // CWaterCannons::Init
    MemPut((void*)0x728CCB, aCannons);      // CWaterCannons::UpdateOne
    MemPut((void*)0x728CEB, aCannons);      // CWaterCannons::UpdateOne
    MemPut((void*)0x728D0D, aCannons);      // CWaterCannons::UpdateOne
    MemPut((void*)0x728D71, aCannons);      // CWaterCannons::UpdateOne
    MemPutFast((void*)0x729B33, aCannons);  // CWaterCannons::Render
    MemPut((void*)0x72A3C5, aCannons);      // CWaterCannons::UpdateOne
    MemPut((void*)0x855432, aCannons);      // 0x855431
    MemPut((void*)0x856BFD, aCannons);      // 0x856BFC

    const auto ucNewLimit = static_cast<BYTE>(newLimit);

    // CWaterCannons::Init
    MemPut(0x728C88, ucNewLimit);

    // CWaterCannons::Update
    MemPut(0x72A3F2, ucNewLimit);

    // CWaterCanons::UpdateOne
    MemPut(0x728CD4, ucNewLimit);
    MemPut(0x728CF6, ucNewLimit);
    MemPut(0x728CFF, ucNewLimit);
    MemPut(0x728D62, ucNewLimit);

    // CWaterCannons::Render
    MemPutFast(0x729B38, ucNewLimit);

    // 0x85542A
    MemPut(0x85542B, ucNewLimit);

    // 0x856BF5
    MemPut(0x856BF6, ucNewLimit);

    // Free previous allocated memory
    if (!isEnabled && currentACannons != nullptr)
        free(currentACannons);

    m_isExtendedWaterCannonsEnabled = isEnabled;
}

void CGameSA::SetRoadSignsTextEnabled(bool isEnabled)
{
    if (isEnabled == m_isRoadSignsTextEnabled)
        return;

    // Skip JMP to CCustomRoadsignMgr::RenderRoadsignAtomic
    MemPut<BYTE>(0x5342ED, isEnabled ? 0xEB : 0x74);

    m_isRoadSignsTextEnabled = isEnabled;
}

void CGameSA::SetIgnoreFireStateEnabled(bool isEnabled)
{
    if (isEnabled == m_isIgnoreFireStateEnabled)
        return;

    if (isEnabled)
    {
        MemSet((void*)0x6511B9, 0x90, 10);  // CCarEnterExit::IsVehicleStealable
        MemSet((void*)0x643A95, 0x90, 14);  // CTaskComplexEnterCar::CreateFirstSubTask
        MemSet((void*)0x6900B5, 0x90, 14);  // CTaskComplexCopInCar::ControlSubTask
        MemSet((void*)0x64F3DB, 0x90, 14);  // CCarEnterExit::IsPlayerToQuitCarEnter

        MemSet((void*)0x685A7F, 0x90, 14);  // CTaskSimplePlayerOnFoot::ProcessPlayerWeapon

        MemSet((void*)0x53A899, 0x90, 5);  // CFire::ProcessFire
        MemSet((void*)0x53A990, 0x90, 5);  // CFire::ProcessFire
    }
    else
    {
        // Restore original bytes
        MemCpy((void*)0x6511B9, "\x88\x86\x90\x04\x00\x00\x85\xC0\x75\x3E", 10);
        MemCpy((void*)0x643A95, "\x8B\x88\x90\x04\x00\x00\x85\xC9\x0F\x85\x99\x01\x00\x00", 14);
        MemCpy((void*)0x6900B5, "\x8B\x81\x90\x04\x00\x00\x85\xC0\x0F\x85\x1A\x01\x00\x00", 14);
        MemCpy((void*)0x64F3DB, "\x8B\x85\x90\x04\x00\x00\x85\xC0\x0F\x85\x1B\x01\x00\x00", 14);

        MemCpy((void*)0x685A7F, "\x8B\x86\x30\x07\x00\x00\x85\xC0\x0F\x85\x1D\x01\x00\x00", 14);

        MemCpy((void*)0x53A899, "\xE8\x82\xF7\x0C\x00", 5);
        MemCpy((void*)0x53A990, "\xE8\x8B\xF6\x0C\x00", 5);
    }

    m_isIgnoreFireStateEnabled = isEnabled;
}

void CGameSA::SetVehicleBurnExplosionsEnabled(bool isEnabled)
{
    if (isEnabled == m_isVehicleBurnExplosionsEnabled)
        return;

    if (isEnabled)
    {
        MemCpy((void*)0x6A74EA, "\xE8\x61\xF5\x08\x00", 5);  // CAutomobile::ProcessCarOnFireAndExplode
        MemCpy((void*)0x737929, "\xE8\x22\xF1\xFF\xFF", 5);  // CExplosion::Update
    }
    else
    {
        MemSet((void*)0x6A74EA, 0x90, 5);
        MemSet((void*)0x737929, 0x90, 5);
    }

    m_isVehicleBurnExplosionsEnabled = isEnabled;
}

bool CGameSA::PerformChecks()
{
    std::map<std::string, SCheatSA*>::iterator it;
    for (it = m_Cheats.begin(); it != m_Cheats.end(); it++)
    {
        if (*(it->second->m_byAddress) != BYTE(it->second->m_bEnabled))
            return false;
    }
    return true;
}
bool CGameSA::VerifySADataFileNames()
{
    return !strcmp(*(char**)0x5B65AE, "DATA\\CARMODS.DAT") && !strcmp(*(char**)0x5BD839, "DATA") && !strcmp(*(char**)0x5BD84C, "HANDLING.CFG") &&
           !strcmp(*(char**)0x5BEEE8, "DATA\\melee.dat") && !strcmp(*(char**)0x4D563E, "ANIM\\PED.IFP") && !strcmp(*(char**)0x5B925B, "DATA\\OBJECT.DAT") &&
           !strcmp(*(char**)0x55D0FC, "data\\surface.dat") && !strcmp(*(char**)0x55F2BB, "data\\surfaud.dat") &&
           !strcmp(*(char**)0x55EB9E, "data\\surfinfo.dat") && !strcmp(*(char**)0x6EAEF8, "DATA\\water.dat") &&
           !strcmp(*(char**)0x6EAEC3, "DATA\\water1.dat") && !strcmp(*(char**)0x5BE686, "DATA\\WEAPON.DAT");
}

void CGameSA::SetAsyncLoadingFromScript(bool bScriptEnabled, bool bScriptForced)
{
    m_bAsyncScriptEnabled = bScriptEnabled;
    m_bAsyncScriptForced = bScriptForced;
}

void CGameSA::SuspendASyncLoading(bool bSuspend, uint uiAutoUnsuspendDelay)
{
    m_bASyncLoadingSuspended = bSuspend;
    // Setup auto unsuspend time if required
    if (uiAutoUnsuspendDelay && bSuspend)
        m_llASyncLoadingAutoUnsuspendTime = CTickCount::Now() + CTickCount((long long)uiAutoUnsuspendDelay);
    else
        m_llASyncLoadingAutoUnsuspendTime = CTickCount();
}

bool CGameSA::IsASyncLoadingEnabled(bool bIgnoreSuspend)
{
    // Process auto unsuspend time if set
    if (m_llASyncLoadingAutoUnsuspendTime.ToLongLong() != 0)
    {
        if (CTickCount::Now() > m_llASyncLoadingAutoUnsuspendTime)
        {
            m_llASyncLoadingAutoUnsuspendTime = CTickCount();
            m_bASyncLoadingSuspended = false;
        }
    }

    if (m_bASyncLoadingSuspended && !bIgnoreSuspend)
        return false;

    if (m_bAsyncScriptForced)
        return m_bAsyncScriptEnabled;
    return true;
}

void CGameSA::SetupSpecialCharacters()
{
    auto* stockCutscenePlayerModelInfo = CModelInfoSAInterface::GetModelInfo(MODEL_CSPLAY);
    auto* stockCutscenePlayerStreamingInfo = m_pStreaming->GetStreamingInfo(MODEL_CSPLAY);
    if (!g_cutscenePlayerMappingsCaptured && stockCutscenePlayerModelInfo && stockCutscenePlayerStreamingInfo &&
        stockCutscenePlayerModelInfo->ulHashKey == m_pKeyGen->GetUppercaseKey("csplay") && stockCutscenePlayerStreamingInfo->sizeInBlocks != 0)
    {
        g_stockCutscenePlayerModelInfo = stockCutscenePlayerModelInfo;
        g_stockCutscenePlayerStreamingInfo = *stockCutscenePlayerStreamingInfo;
        g_stockCutscenePlayerStreamingInfo.prevId = UINT16_MAX;
        g_stockCutscenePlayerStreamingInfo.nextId = UINT16_MAX;
        g_stockCutscenePlayerStreamingInfo.loadState = eModelLoadState::LOADSTATE_NOT_LOADED;
        g_cutscenePlayerMappingsCaptured = true;
    }

    if (!g_cutsceneObjectMappingsCaptured)
    {
        bool capturedEveryMapping = true;
        for (std::size_t index = 0; index < MTA_CUTSCENE_OBJECT_SLOT_COUNT; ++index)
        {
            const unsigned int modelId = MODEL_CUTOBJ01 + static_cast<unsigned int>(index);
            auto*              modelInfo = CModelInfoSAInterface::GetModelInfo(modelId);
            auto*              streamingInfo = m_pStreaming->GetStreamingInfo(modelId);
            if (!modelInfo || !streamingInfo)
            {
                capturedEveryMapping = false;
                break;
            }

            g_stockCutsceneObjectModelInfos[index] = modelInfo;
            g_stockCutsceneObjectStreamingInfos[index] = *streamingInfo;
            g_stockCutsceneObjectStreamingInfos[index].prevId = UINT16_MAX;
            g_stockCutsceneObjectStreamingInfos[index].nextId = UINT16_MAX;
            g_stockCutsceneObjectStreamingInfos[index].loadState = eModelLoadState::LOADSTATE_NOT_LOADED;
        }
        g_cutsceneObjectMappingsCaptured = capturedEveryMapping;
    }

    ModelInfo[1].MakePedModel("TRUTH");
    g_mtaSpecialCharacterModelInfo = CModelInfoSAInterface::GetModelInfo(MODEL_CSPLAY);
    ModelInfo[2].MakePedModel("MACCER");

    ModelInfo[3].MakePedModel("CDEPUT");
    ModelInfo[4].MakePedModel("SFPDM1");
    ModelInfo[5].MakePedModel("BB");
    ModelInfo[6].MakePedModel("WFYCRP");
    ModelInfo[8].MakePedModel("WMYCD2");
    ModelInfo[42].MakePedModel("SUZIE");
    ModelInfo[65].MakePedModel("VWMYAP");
    ModelInfo[86].MakePedModel("VHFYST");
    ModelInfo[119].MakePedModel("LVPDM1");

    ModelInfo[265].MakePedModel("TENPEN");
    ModelInfo[266].MakePedModel("PULASKI");
    ModelInfo[267].MakePedModel("HERN");
    ModelInfo[268].MakePedModel("DWAYNE");
    ModelInfo[269].MakePedModel("SMOKE");
    ModelInfo[270].MakePedModel("SWEET");
    ModelInfo[271].MakePedModel("RYDER");
    ModelInfo[272].MakePedModel("FORELLI");
    ModelInfo[273].MakePedModel("MEDIATR");
    ModelInfo[289].MakePedModel("SOMYAP");
    ModelInfo[290].MakePedModel("ROSE");
    ModelInfo[291].MakePedModel("PAUL");
    ModelInfo[292].MakePedModel("CESAR");
    ModelInfo[293].MakePedModel("OGLOC");
    ModelInfo[294].MakePedModel("WUZIMU");
    ModelInfo[295].MakePedModel("TORINO");
    ModelInfo[296].MakePedModel("JIZZY");
    ModelInfo[297].MakePedModel("MADDOGG");
    ModelInfo[298].MakePedModel("CAT");
    ModelInfo[299].MakePedModel("CLAUDE");
    for (std::size_t index = 0; index < MTA_CUTSCENE_OBJECT_SLOT_COUNT; ++index)
        ModelInfo[MODEL_CUTOBJ01 + index].MakePedModel(MTA_CUTSCENE_OBJECT_SPECIAL_CHARACTER_NAMES[index]);

    // ModelInfo[190].MakePedModel ( "BARBARA" );
    // ModelInfo[191].MakePedModel ( "HELENA" );
    // ModelInfo[192].MakePedModel ( "MICHELLE" );
    // ModelInfo[193].MakePedModel ( "KATIE" );
    // ModelInfo[194].MakePedModel ( "MILLIE" );
    // ModelInfo[195].MakePedModel ( "DENISE" );
    /* Hot-coffee only models
    ModelInfo[313].MakePedModel ( "GANGRL2" );
    ModelInfo[314].MakePedModel ( "MECGRL2" );
    ModelInfo[315].MakePedModel ( "GUNGRL2" );
    ModelInfo[316].MakePedModel ( "COPGRL2" );
    ModelInfo[317].MakePedModel ( "NURGRL2" );
    */
}

void CGameSA::FixModelCol(uint iFixModel, uint iFromModel)
{
    CBaseModelInfoSAInterface* pFixModelInterface = ModelInfo[iFixModel].GetInterface();
    if (!pFixModelInterface || pFixModelInterface->pColModel)
        return;

    CBaseModelInfoSAInterface* pAviableModelInterface = ModelInfo[iFromModel].GetInterface();

    if (!pAviableModelInterface)
        return;

    pFixModelInterface->pColModel = pAviableModelInterface->pColModel;
}

void CGameSA::SetupBrokenModels()
{
    FixModelCol(3118, 3059);
    FixModelCol(3553, 3554);
}

// Ensure replaced/restored textures for models in the GTA map are correct
void CGameSA::FlushPendingRestreamIPL()
{
    CModelInfoSA::StaticFlushPendingRestreamIPL();
    m_pRenderWare->ResetStats();
}

void CGameSA::GetShaderReplacementStats(SShaderReplacementStats& outStats)
{
    m_pRenderWare->GetShaderReplacementStats(outStats);
}

void CGameSA::RemoveGameWorld()
{
    m_pIplStore->SetDynamicIplStreamingEnabled(false);

    m_pCoverManager->RemoveAllCovers();
    m_pPlantManager->RemoveAllPlants();

    // Remove all shadows in CStencilShadowObjects::dtorAll
    ((void* (*)())0x711390)();

    m_Pools->GetDummyPool().RemoveAllWithBackup();
    m_Pools->GetBuildingsPool().RemoveAllWithBackup();

    static_cast<CBuildingRemovalSA*>(m_pBuildingRemoval)->DropCaches();

    m_isGameWorldRemoved = true;
}

void CGameSA::RestoreGameWorld()
{
    m_Pools->GetBuildingsPool().RestoreBackup();
    m_Pools->GetDummyPool().RestoreBackup();

    m_pIplStore->SetDynamicIplStreamingEnabled(true, [](CIplSAInterface* ipl) { return memcmp("barriers", ipl->name, 8) != 0; });
    m_isGameWorldRemoved = false;
}

bool CGameSA::SetBuildingPoolSize(size_t size)
{
    // IplDef stores building range endpoints as signed 16-bit values. The
    // checkpoint's closed capacity deliberately stays below that hard limit.
    if (size > MAX_BUILDINGS)
        return false;

    const bool shouldRemoveWorld = !m_isGameWorldRemoved;

    const int iCurrentBuildingPoolSize = m_Pools->GetBuildingsPool().GetSize();
    if (iCurrentBuildingPoolSize >= 0 && static_cast<size_t>(iCurrentBuildingPoolSize) == size)
    {
        // Keep same-size behavior unchanged while world is active.
        // If world is already removed, skip no-op resize and only drop caches.
        if (!shouldRemoveWorld)
        {
            static_cast<CBuildingRemovalSA*>(m_pBuildingRemoval)->DropCaches();
            return true;
        }

        // World is active here, so continue with remove and restore flow.
    }

    if (shouldRemoveWorld)
        RemoveGameWorld();
    else
        static_cast<CBuildingRemovalSA*>(m_pBuildingRemoval)->DropCaches();

    bool status = m_Pools->GetBuildingsPool().Resize(size);

    if (status && iCurrentBuildingPoolSize > 0 && size < static_cast<size_t>(iCurrentBuildingPoolSize))
    {
        // First let native IPL unloading remove every entity against the old
        // capacity, then make persistent metadata safe before restoration.
        static_cast<CIplStoreSA*>(m_pIplStore)->ClampBuildingRanges(size);
    }

    if (shouldRemoveWorld)
        RestoreGameWorld();

    return status;
}

// Ensure models have the default lod distances
void CGameSA::ResetModelLodDistances()
{
    CModelInfoSA::StaticResetLodDistances();
}

void CGameSA::ResetModelFlags()
{
    CModelInfoSA::StaticResetFlags();
}

void CGameSA::ResetModelTimes()
{
    CModelInfoSA::StaticResetModelTimes();
}

void CGameSA::ResetAlphaTransparencies()
{
    CModelInfoSA::StaticResetAlphaTransparencies();
}

// Disable VSync by forcing what normally happends at the end of the loading screens
// Note #1: This causes the D3D device to be reset after the next frame
// Note #2: Some players do not need this to disable VSync. (Possibly because their video card driver settings override it somewhere)
void CGameSA::DisableVSync()
{
    MemPutFast<BYTE>(0xBAB318, 0);  // CLoadingScreen::m_bActive
}
CWeapon* CGameSA::CreateWeapon()
{
    return new CWeaponSA(new CWeaponSAInterface, NULL, WEAPONSLOT_MAX);
}

CWeaponStat* CGameSA::CreateWeaponStat(eWeaponType weaponType, eWeaponSkill weaponSkill)
{
    return m_pWeaponStatsManager->CreateWeaponStatUnlisted(weaponType, weaponSkill);
}

void CGameSA::SetWeaponRenderEnabled(bool enabled)
{
    if (IsWeaponRenderEnabled() == enabled)
        return;

    if (!enabled)
    {
        // Disable calls to CVisibilityPlugins::RenderWeaponPedsForPC
        MemSet((void*)0x53EAC4, 0x90, 5);  // Idle
        MemSet((void*)0x705322, 0x90, 5);  // CPostEffects::Render
        MemSet((void*)0x7271E3, 0x90, 5);  // CMirrors::BeforeMainRender
    }
    else
    {
        // Restore original bytes
        MemCpy((void*)0x53EAC4, "\xE8\x67\x44\x1F\x00", 5);
        MemCpy((void*)0x705322, "\xE8\x09\xDC\x02\x00", 5);
        MemCpy((void*)0x7271E3, "\xE8\x48\xBD\x00\x00", 5);
    }
}

bool CGameSA::IsWeaponRenderEnabled() const
{
    return *(unsigned char*)0x53EAC4 == 0xE8;
}

void CGameSA::OnPedContextChange(CPed* pPedContext)
{
    m_pPedContext = pPedContext;
}

CPed* CGameSA::GetPedContext()
{
    if (!m_pPedContext)
        m_pPedContext = pGame->GetPools()->GetPedFromRef((DWORD)1);
    return m_pPedContext;
}

CObjectGroupPhysicalProperties* CGameSA::GetObjectGroupPhysicalProperties(unsigned char ucObjectGroup)
{
    if (ucObjectGroup < OBJECTDYNAMICINFO_MAX && ObjectGroupsInfo[ucObjectGroup].IsValid())
        return &ObjectGroupsInfo[ucObjectGroup];

    return nullptr;
}

bool CGameSA::RequestVehicleRecording(int recordingId)
{
    // Vanilla silently falls back to streaming slot zero for unknown numbers.
    // Reject them here so a resource typo cannot request or later play another
    // recording by accident.
    if (!IsKnownVehicleRecording(recordingId))
        return false;

    reinterpret_cast<void(__cdecl*)(int)>(FUNC_RequestVehicleRecording)(recordingId);
    return true;
}

bool CGameSA::IsVehicleRecordingLoaded(int recordingId)
{
    if (!IsKnownVehicleRecording(recordingId))
        return false;

    return reinterpret_cast<bool(__cdecl*)(int)>(FUNC_IsVehicleRecordingLoaded)(recordingId);
}

bool CGameSA::StartVehiclePlayback(CVehicle* vehicle, int recordingId)
{
    if (!vehicle || !IsVehicleRecordingLoaded(recordingId) || IsVehiclePlaybackActive(vehicle) || !HasFreeVehiclePlaybackSlot())
        return false;

    // This is the direct 05EB path used by SWEET1. AI and looping variants are
    // intentionally not inferred from optional Lua flags until their distinct
    // lifecycle and synchronization behavior has its own conformance slice.
    reinterpret_cast<void(__cdecl*)(CVehicleSAInterface*, int, bool, bool)>(FUNC_StartVehiclePlayback)(vehicle->GetVehicleInterface(), recordingId, false,
                                                                                                       false);
    return IsVehiclePlaybackActive(vehicle);
}

bool CGameSA::StopVehiclePlayback(CVehicle* vehicle)
{
    if (!vehicle || !IsVehiclePlaybackActive(vehicle))
        return false;

    reinterpret_cast<void(__cdecl*)(CVehicleSAInterface*)>(FUNC_StopVehiclePlayback)(vehicle->GetVehicleInterface());
    return !IsVehiclePlaybackActive(vehicle);
}

bool CGameSA::IsVehiclePlaybackActive(CVehicle* vehicle)
{
    if (!vehicle)
        return false;

    return reinterpret_cast<bool(__cdecl*)(CVehicleSAInterface*)>(FUNC_IsVehiclePlaybackActive)(vehicle->GetVehicleInterface());
}

bool CGameSA::RemoveVehicleRecording(int recordingId)
{
    if (!IsKnownVehicleRecording(recordingId))
        return false;

    reinterpret_cast<void(__cdecl*)(int)>(FUNC_RemoveVehicleRecording)(recordingId);
    return true;
}

bool CGameSA::SetVehiclePlaybackSpeed(CVehicle* vehicle, float speed)
{
    if (!vehicle || !std::isfinite(speed) || speed < 0.0f || !IsVehiclePlaybackActive(vehicle))
        return false;

    // GTA:SA 1.0 0x459660 finds the active recording slot for this vehicle and
    // writes the raw float to PlaybackSpeed[slot]. The active-slot guard keeps
    // a stale resource from configuring a later occupant of the global pool.
    reinterpret_cast<void(__cdecl*)(CVehicleSAInterface*, float)>(FUNC_SetVehiclePlaybackSpeed)(vehicle->GetVehicleInterface(), speed);
    return IsVehiclePlaybackActive(vehicle);
}

bool CGameSA::LoadMissionTextBlock(const char* blockName)
{
    if (!blockName || !blockName[0])
        return false;

    // LOAD_MISSION_TEXT clears queues before replacing the backing mission
    // block. The client-side lease manager therefore clears and relinquishes
    // every pointer it owns before this call can be made for another block.
    reinterpret_cast<void(__thiscall*)(void*, const char*)>(FUNC_LoadMissionText)(reinterpret_cast<void*>(GTA_TEXT), blockName);

    char loadedName[8]{};
    reinterpret_cast<void(__thiscall*)(void*, char*)>(FUNC_GetLoadedMissionText)(reinterpret_cast<void*>(GTA_TEXT), loadedName);
    return _stricmp(loadedName, blockName) == 0;
}

bool CGameSA::ShowMissionText(const char* key, unsigned int duration, unsigned short flags)
{
    const char* text = GetMissionText(key);
    if (!text)
        return false;

    // PRINT_NOW suppresses only spoken (~z~) subtitles when the player's GTA
    // subtitle option is disabled. Objective text always enters the queue.
    const bool subtitlesEnabled = *reinterpret_cast<const bool*>(GTA_SETTINGS + GTA_SUBTITLES_OFFSET);
    if (!subtitlesEnabled && std::strncmp(text, "~z~", 3) == 0)
        return true;

    reinterpret_cast<void(__cdecl*)(const char*, unsigned int, unsigned short, bool)>(FUNC_AddMessageJump)(text, duration, flags, false);
    return true;
}

bool CGameSA::ShowMissionHelp(const char* key, bool permanent)
{
    const char* text = GetMissionText(key);
    if (!text)
        return false;

    reinterpret_cast<void(__cdecl*)(const char*, bool, bool, bool)>(FUNC_SetHelpMessage)(text, false, permanent, false);
    return true;
}

bool CGameSA::ShowMissionBigText(const char* key, unsigned int duration, unsigned int style, bool hasNumber, int number)
{
    if (style == 0 || style > GTA_BIG_MESSAGE_STYLE_COUNT)
        return false;

    const char* text = GetMissionText(key);
    if (!text)
        return false;

    const unsigned int nativeStyle = style - 1;
    if (hasNumber)
    {
        reinterpret_cast<void(__cdecl*)(const char*, unsigned int, unsigned int, int, int, int, int, int, int)>(FUNC_AddBigMessageWithNumber)(
            text, duration, nativeStyle, number, -1, -1, -1, -1, -1);
    }
    else
    {
        reinterpret_cast<void(__cdecl*)(const char*, unsigned int, unsigned int)>(FUNC_AddBigMessage)(text, duration, nativeStyle);
    }
    return true;
}

void CGameSA::ClearMissionText(const char* key, bool big)
{
    const char* text = GetMissionText(key);
    if (!text)
        return;

    if (big)
        reinterpret_cast<void(__cdecl*)(const char*)>(FUNC_ClearThisBigPrint)(text);
    else
        reinterpret_cast<void(__cdecl*)(const char*)>(FUNC_ClearThisPrint)(text);
}

void CGameSA::ClearMissionHelp()
{
    // This is the exact CLEAR_HELP argument tuple: null text, quick=true,
    // permanent=false, and no previous-brief insertion.
    reinterpret_cast<void(__cdecl*)(const char*, bool, bool, bool)>(FUNC_SetHelpMessage)(nullptr, true, false, false);
}

bool CGameSA::LoadFileCutscene(const char* name)
{
    if (!name || !name[0] || std::strlen(name) > 7 || IsFileCutsceneActive())
        return false;

    // Passing an unknown name into CCutsceneMgr still clears zone models and
    // hides the player before it discovers the missing files. The native
    // cutscene-track table is a compact stock-name oracle, so reject anything
    // outside it before mutating global engine state.
    if (reinterpret_cast<short(__cdecl*)(const char*)>(FUNC_FindCutsceneAudioTrack)(name) < 0)
        return false;

    // Validate the captured stock mappings now, but do not reject the lease
    // merely because a ped is still releasing one of MTA's repurposed model
    // slots. IsFileCutsceneLoaded retries the atomic remap after ordinary
    // streaming finishes the model transition.
    if (!ValidateManagedFileCutsceneModelMappings(m_pStreaming))
        return false;

    g_managedFileCutsceneBlockingModel = UINT_MAX;
    g_managedFileCutsceneBlockingRefs = 0;
    g_preloadingManagedFileCutscene = true;
    std::memcpy(g_managedFileCutsceneName, name, std::strlen(name) + 1);
    g_suppressManagedFileCutsceneSkipInput = true;
    return true;
}

bool CGameSA::IsFileCutsceneActive() const
{
    const int  loadStatus = *reinterpret_cast<const int*>(VAR_FileCutsceneLoadStatus);
    const bool running = *reinterpret_cast<const bool*>(VAR_FileCutsceneRunning);
    const bool processing = *reinterpret_cast<const bool*>(VAR_FileCutsceneProcessing);
    return g_preloadingManagedFileCutscene || loadStatus != 0 || running || processing;
}

bool CGameSA::IsFileCutsceneLoaded() const
{
    if (g_preloadingManagedFileCutscene)
    {
        if (!g_restoreManagedFileCutsceneModelMappings)
        {
            // The core may recache a still-needed model while a real owner is
            // draining. Evict its revocable pin on every poll, then perform
            // the double-zero test and mapping install synchronously in this
            // same game-thread call.
            if (!EvictManagedFileCutsceneModelCachePins())
                return false;

            unsigned int blockingModel{};
            unsigned int blockingNativeReferences{};
            int          blockingMtaReferences{};
            if (GetManagedFileCutsceneMappingBlocker(m_pStreaming, blockingModel, blockingNativeReferences, blockingMtaReferences))
            {
                ReportManagedFileCutsceneMappingWait(blockingModel, blockingNativeReferences, blockingMtaReferences);
                return false;
            }

            // The validation pass above does not mutate mappings. Install only
            // once every MTA request and GTA instance reference is gone, so
            // live peds and resource-owned model requests can never retain a
            // clump whose global slot is being repurposed underneath them.
            // Keep the installed mapping across later polling pulses while
            // ordinary streaming finishes loading CSPLAY.
            if (!InstallManagedFileCutsceneModelMappings(m_pStreaming))
                return false;

            ReportManagedFileCutsceneMappingReady();
            g_restoreManagedFileCutsceneModelMappings = true;
        }

        // CStreaming::LoadAllRequestedModels cannot safely be nested inside
        // the Lua pulse that requested the cutscene. Let ordinary game pulses
        // finish CSPLAY, then enter GTA's native loader with the correct base
        // clump already resident.
        if (!m_pStreaming->HasModelLoaded(MODEL_CSPLAY))
            return false;

        auto* cutscenePlayerModel = CModelInfoSAInterface::GetModelInfo(MODEL_CSPLAY);
        if (!cutscenePlayerModel || cutscenePlayerModel->ulHashKey != m_pKeyGen->GetUppercaseKey("csplay") || !cutscenePlayerModel->pRwObject ||
            !AreManagedFileCutsceneModelMappingsInstalled())
            return false;

        // Vanilla missions disable world streaming immediately before
        // LOAD_CUTSCENE. CCutsceneMgr restores this flag after postload or
        // teardown, so preserve that native loading window for managed scenes.
        *reinterpret_cast<bool*>(VAR_DisableStreaming) = true;
        reinterpret_cast<void(__cdecl*)(const char*)>(FUNC_LoadFileCutscene)(g_managedFileCutsceneName);
        g_preloadingManagedFileCutscene = false;
        if (!IsFileCutsceneActive())
        {
            *reinterpret_cast<bool*>(VAR_DisableStreaming) = false;
            RestoreManagedFileCutsceneModelMappings(m_pStreaming);
            return false;
        }
    }

    return *reinterpret_cast<const int*>(VAR_FileCutsceneLoadStatus) == 2 && RestoreManagedFileCutsceneObjectAreas();
}

bool CGameSA::StartFileCutscene()
{
    if (!IsFileCutsceneLoaded() || *reinterpret_cast<const bool*>(VAR_FileCutsceneRunning))
        return false;

    reinterpret_cast<void(__cdecl*)()>(FUNC_StartFileCutscene)();
    return true;
}

bool CGameSA::HasFileCutsceneFinished() const
{
    return reinterpret_cast<bool(__cdecl*)()>(FUNC_HasFileCutsceneFinished)();
}

bool CGameSA::IsFileCutsceneSkipInputPressed() const
{
    return reinterpret_cast<bool(__cdecl*)()>(FUNC_IsFileCutsceneSkipInputPressed)();
}

bool CGameSA::WasFileCutsceneSkipped() const
{
    return *reinterpret_cast<const bool*>(VAR_FileCutsceneSkipped);
}

bool CGameSA::SkipFileCutscene()
{
    if (!IsFileCutsceneLoaded() || !*reinterpret_cast<const bool*>(VAR_FileCutsceneRunning))
        return false;

    reinterpret_cast<void(__cdecl*)()>(FUNC_SkipFileCutscene)();
    return WasFileCutsceneSkipped();
}

bool CGameSA::DeleteFileCutscene()
{
    g_suppressManagedFileCutsceneSkipInput = false;
    g_preloadingManagedFileCutscene = false;
    if (IsFileCutsceneActive())
        reinterpret_cast<void(__cdecl*)()>(FUNC_DeleteFileCutscene)();

    const bool deleted = !IsFileCutsceneActive();
    RestoreManagedFileCutsceneModelMappings(m_pStreaming);
    g_managedFileCutsceneBlockingModel = UINT_MAX;
    g_managedFileCutsceneBlockingRefs = 0;
    return deleted;
}
