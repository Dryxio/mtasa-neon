/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskCarSA.cpp
 *  PURPOSE:     Car game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "TaskCarSA.h"

#include <limits>

namespace
{
    constexpr std::uintptr_t GTA_PATH_FIND = 0x96F050;
    constexpr std::uintptr_t FUNC_CCarCtrl_JoinCarWithRoadSystem = 0x42F5A0;
    constexpr std::uintptr_t FUNC_CCarCtrl_FindLinksToGoWithTheseNodes = 0x42B470;
    constexpr std::uintptr_t CALL_CTaskComplexCarDriveWander_JoinCarWithRoadSystem = 0x63CB91;

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

    constexpr unsigned int VEHICLE_AUTOPILOT_CURRENT_NODE_OFFSET = 0x390;
    constexpr unsigned int VEHICLE_AUTOPILOT_STARTING_NODE_OFFSET = 0x394;
    constexpr unsigned int VEHICLE_AUTOPILOT_CURRENT_LANE_OFFSET = 0x3B7;
    constexpr unsigned int VEHICLE_AUTOPILOT_NEXT_LANE_OFFSET = 0x3B8;
    constexpr unsigned int VEHICLE_AUTOPILOT_NEXT_LINK_DIRECTION_OFFSET = 0x3B6;

    struct SCarPathNodeAddress
    {
        unsigned short area;
        unsigned short node;
    };
    static_assert(sizeof(SCarPathNodeAddress) == 4, "Invalid car path-node address size");

    struct SDirectedLaneData
    {
        const unsigned char* carLink{};
        unsigned int         laneCount{};
        unsigned int         lanesTowardAttached{};
        unsigned int         lanesAwayFromAttached{};
    };

    bool GetDirectedLaneData(const SCarPathNodeAddress& from, const SCarPathNodeAddress& to, SDirectedLaneData& output)
    {
        output = {};
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

        auto* const* nodeLinks = reinterpret_cast<SCarPathNodeAddress* const*>(GTA_PATH_FIND + PATH_NODE_LINK_ARRAY_OFFSET);
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
        const auto&          attachedTo = *reinterpret_cast<const SCarPathNodeAddress*>(carLink + 0x04);
        const bool           attachedToFrom = attachedTo.area == from.area && attachedTo.node == from.node;
        const bool           attachedToTo = attachedTo.area == to.area && attachedTo.node == to.node;
        if (!attachedToFrom && !attachedToTo)
            return false;

        const unsigned char laneFlags = carLink[0x0B];
        output.carLink = carLink;
        output.lanesTowardAttached = laneFlags & 0x07;
        output.lanesAwayFromAttached = (laneFlags >> 3) & 0x07;
        output.laneCount = attachedToTo ? output.lanesTowardAttached : output.lanesAwayFromAttached;
        return true;
    }

    unsigned char FindNearestLane(const CVehicleSAInterface* vehicle, const SDirectedLaneData& laneData)
    {
        if (!vehicle || !laneData.carLink || laneData.laneCount == 0)
            return 0;

        const float linkX = static_cast<float>(*reinterpret_cast<const short*>(laneData.carLink + 0x00)) / 8.0f;
        const float linkY = static_cast<float>(*reinterpret_cast<const short*>(laneData.carLink + 0x02)) / 8.0f;
        const float directionSign = static_cast<float>(
            *reinterpret_cast<const signed char*>(reinterpret_cast<const unsigned char*>(vehicle) + VEHICLE_AUTOPILOT_NEXT_LINK_DIRECTION_OFFSET));
        const float    directionX = static_cast<float>(*reinterpret_cast<const signed char*>(laneData.carLink + 0x08)) * 0.01f * directionSign;
        const float    directionY = static_cast<float>(*reinterpret_cast<const signed char*>(laneData.carLink + 0x09)) * 0.01f * directionSign;
        const float    oneWayOffset = laneData.lanesTowardAttached == 0      ? 0.5f - 0.5f * laneData.lanesAwayFromAttached
                                      : laneData.lanesAwayFromAttached == 0 ? 0.5f - 0.5f * laneData.lanesTowardAttached
                                                                            : static_cast<float>(static_cast<signed char>(laneData.carLink[0x0A])) / 86.4f + 0.5f;
        const CVector& position = vehicle->matrix ? vehicle->matrix->vPos : vehicle->m_transform.m_translate;

        unsigned char nearestLane = 0;
        float         nearestDistanceSquared = std::numeric_limits<float>::max();
        for (unsigned int lane = 0; lane < laneData.laneCount; ++lane)
        {
            const float laneOffset = (oneWayOffset + lane) * 5.4f;
            const float laneX = linkX + laneOffset * directionY;
            const float laneY = linkY - laneOffset * directionX;
            const float deltaX = position.fX - laneX;
            const float deltaY = position.fY - laneY;
            const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
            if (distanceSquared < nearestDistanceSquared)
            {
                nearestDistanceSquared = distanceSquared;
                nearestLane = static_cast<unsigned char>(lane);
            }
        }
        return nearestLane;
    }

    void __cdecl JoinDriveWanderVehicleWithLegalRoadDirection(CVehicleSAInterface* vehicle)
    {
        using JoinCarWithRoadSystem = void(__cdecl*)(CVehicleSAInterface*);
        reinterpret_cast<JoinCarWithRoadSystem>(FUNC_CCarCtrl_JoinCarWithRoadSystem)(vehicle);
        if (!vehicle)
            return;

        auto* const       bytes = reinterpret_cast<unsigned char*>(vehicle);
        auto&             currentNode = *reinterpret_cast<SCarPathNodeAddress*>(bytes + VEHICLE_AUTOPILOT_CURRENT_NODE_OFFSET);
        auto&             startingNode = *reinterpret_cast<SCarPathNodeAddress*>(bytes + VEHICLE_AUTOPILOT_STARTING_NODE_OFFSET);
        SDirectedLaneData selectedDirection{};
        if (!GetDirectedLaneData(currentNode, startingNode, selectedDirection) || selectedDirection.laneCount != 0)
            return;

        SDirectedLaneData reverseDirection{};
        if (!GetDirectedLaneData(startingNode, currentNode, reverseDirection) || reverseDirection.laneCount == 0)
            return;

        // JoinCarWithRoadSystem chooses the shortest adjacent edge and can bind
        // a script-created Wander vehicle against a one-way road. Vanilla
        // ambient cars already carry a lane-valid route and never take this
        // generic join path. Reverse only an impossible directed edge, then
        // rebuild the native link state and retain the physical lane nearest
        // the synchronized vehicle pose.
        std::swap(currentNode, startingNode);
        using FindLinksToGoWithTheseNodes = void(__cdecl*)(CVehicleSAInterface*);
        reinterpret_cast<FindLinksToGoWithTheseNodes>(FUNC_CCarCtrl_FindLinksToGoWithTheseNodes)(vehicle);

        if (!GetDirectedLaneData(currentNode, startingNode, reverseDirection) || reverseDirection.laneCount == 0)
            return;
        const unsigned char lane = FindNearestLane(vehicle, reverseDirection);
        bytes[VEHICLE_AUTOPILOT_CURRENT_LANE_OFFSET] = lane;
        bytes[VEHICLE_AUTOPILOT_NEXT_LANE_OFFSET] = lane;
    }
}

void InstallTaskCarSAHooks()
{
    HookInstallCall(CALL_CTaskComplexCarDriveWander_JoinCarWithRoadSystem, reinterpret_cast<DWORD>(&JoinDriveWanderVehicleWithLegalRoadDirection));
}

CTaskSimpleBikeJackedSA::CTaskSimpleBikeJackedSA(CVehicle* pVehicle, int iDoor, int iDraggedPedDownTime, CPed* pJacker, bool bVictimIsDriver)
{
    auto* pVehicleSA = dynamic_cast<CVehicleSA*>(pVehicle);
    auto* pJackerSA = dynamic_cast<CPedSA*>(pJacker);
    if (!pVehicleSA || !pJackerSA)
        return;

    CreateTaskInterface(sizeof(CTaskSimpleBikeJackedSAInterface));
    if (!IsValid())
        return;

    const DWORD dwFunc = FUNC_CTaskSimpleBikeJacked__Constructor;
    const DWORD dwThisInterface = reinterpret_cast<DWORD>(GetInterface());
    const DWORD dwVehicle = reinterpret_cast<DWORD>(pVehicleSA->GetInterface());
    const DWORD dwJacker = reinterpret_cast<DWORD>(pJackerSA->GetInterface());

    // The retail constructor registers safe references to both entities and
    // owns the complete BIKE_HIT -> knock-off lifecycle.
    // clang-format off
    __asm
    {
        push    ebx
        xor     ebx, ebx
        movzx   ebx, bVictimIsDriver
        push    ebx
        push    dwJacker
        push    iDraggedPedDownTime
        push    iDoor
        push    dwVehicle
        mov     ecx, dwThisInterface
        call    dwFunc
        pop     ebx
    }
    // clang-format on
}

// ##############################################################################
// ## Name:    CTaskComplexEnterCar
// ## Purpose: Makes the ped enter the specified vehicle
// ## Notes:   Shouldn't be used directly, use CTaskComplexEnterCarAsDriver or
// ##          CTaskComplexEnterCarAsPassenger instead
// ##############################################################################

CTaskComplexEnterCarSA::CTaskComplexEnterCarSA(CVehicle* pTargetVehicle, const bool bAsDriver, const bool bQuitAfterOpeningDoor,
                                               const bool bQuitAfterDraggingPedOut, const bool bCarryOnAfterFallingOff)
    : CTaskComplexSA()
{
}

// ##############################################################################
// ## Name:    CTaskComplexEnterCarAsDriver
// ## Purpose: Makes the ped enter the specified vehicle
// ##############################################################################

CTaskComplexEnterCarAsDriverSA::CTaskComplexEnterCarAsDriverSA(CVehicle* pTargetVehicle) : CTaskComplexEnterCarSA(pTargetVehicle, true, false, false, false)
{
    CVehicleSA* pTargetVehicleSA = dynamic_cast<CVehicleSA*>(pTargetVehicle);

    if (pTargetVehicleSA)
    {
        CreateTaskInterface(sizeof(CTaskComplexEnterCarAsDriverSAInterface));
        if (!IsValid())
            return;
        DWORD dwFunc = FUNC_CTaskComplexEnterCarAsDriver__Constructor;
        DWORD dwVehiclePtr = (DWORD)pTargetVehicleSA->GetInterface();
        DWORD dwThisInterface = (DWORD)GetInterface();

        // clang-format off
        __asm
        {
            mov     ecx, dwThisInterface
            push    dwVehiclePtr
            call    dwFunc
        }
        // clang-format on
    }
}

// ##############################################################################
// ## Name:    CTaskComplexEnterCarAsPassenger
// ## Purpose: Makes the ped enter the specified vehicle as a passenger
// ##############################################################################

CTaskComplexEnterCarAsPassengerSA::CTaskComplexEnterCarAsPassengerSA(CVehicle* pTargetVehicle, const int iTargetSeat, const bool bCarryOnAfterFallingOff)
    : CTaskComplexEnterCarSA(pTargetVehicle, false, false, false, false)
{
    CVehicleSA* pTargetVehicleSA = dynamic_cast<CVehicleSA*>(pTargetVehicle);

    if (pTargetVehicleSA)
    {
        CreateTaskInterface(sizeof(CTaskComplexEnterCarAsPassengerSAInterface));
        if (!IsValid())
            return;
        DWORD dwFunc = FUNC_CTaskComplexEnterCarAsPassenger__Constructor;
        DWORD dwVehiclePtr = (DWORD)pTargetVehicleSA->GetInterface();
        DWORD dwThisInterface = (DWORD)GetInterface();

        // clang-format off
        __asm
        {
            push    edx
            xor     edx, edx
            movzx   edx, bCarryOnAfterFallingOff
            mov     ecx, dwThisInterface
            push    edx
            push    iTargetSeat
            push    dwVehiclePtr
            call    dwFunc
            pop     edx
        }
        // clang-format on
    }
}

// ##############################################################################
// ## Name:    CTaskComplexEnterBoatAsDriver
// ## Purpose: Makes the ped enter the specified boat as the driver
// ##############################################################################

CTaskComplexEnterBoatAsDriverSA::CTaskComplexEnterBoatAsDriverSA(CVehicle* pTargetVehicle) : CTaskComplexSA()
{
    CVehicleSA* pTargetVehicleSA = dynamic_cast<CVehicleSA*>(pTargetVehicle);

    if (pTargetVehicleSA)
    {
        CreateTaskInterface(sizeof(CTaskComplexEnterBoatAsDriverSAInterface));
        if (!IsValid())
            return;
        DWORD dwFunc = FUNC_CTaskComplexEnterBoatAsDriver__Constructor;
        DWORD dwVehiclePtr = (DWORD)pTargetVehicleSA->GetInterface();
        DWORD dwThisInterface = (DWORD)GetInterface();

        // clang-format off
        __asm
        {
            mov     ecx, dwThisInterface
            push    dwVehiclePtr
            call    dwFunc
        }
        // clang-format on
    }
}

// ##############################################################################
// ## Name:    CTaskComplexLeaveCar
// ## Purpose: Makes the ped leave a specific vehicle
// ##############################################################################

CTaskComplexLeaveCarSA::CTaskComplexLeaveCarSA(CVehicle* pTargetVehicle, const int iTargetDoor, const int iDelayTime, const bool bSensibleLeaveCar,
                                               const bool bForceGetOut)
    : CTaskComplexSA()
{
    CVehicleSA* pTargetVehicleSA = dynamic_cast<CVehicleSA*>(pTargetVehicle);

    if (pTargetVehicleSA)
    {
        CreateTaskInterface(sizeof(CTaskComplexLeaveCarSAInterface));
        if (!IsValid())
            return;
        DWORD      dwFunc = FUNC_CTaskComplexLeaveCar__Constructor;
        DWORD      dwVehiclePtr = (DWORD)pTargetVehicleSA->GetInterface();
        DWORD      dwThisInterface = (DWORD)GetInterface();
        DWORD      dwDoorIdx = 0;
        static int s_iCarNodeIndexes[6] = {0x10, 0x11, 0x0A, 0x08, 0x0B, 0x09};

        if (iTargetDoor >= 0 && iTargetDoor <= 5)
            dwDoorIdx = s_iCarNodeIndexes[iTargetDoor];

        // clang-format off
        __asm
        {
            mov     ecx, dwThisInterface
            push    ebx
            xor     ebx, ebx
            movzx   ebx, bForceGetOut
            push    ebx
            movzx   ebx, bSensibleLeaveCar
            push    ebx
            push    iDelayTime
            push    dwDoorIdx
            push    dwVehiclePtr
            call    dwFunc
            pop     ebx
        }
        // clang-format on
    }
}

// ##############################################################################
// ## Name:    CTaskComplexCarDriveWander
// ## Purpose: Lets GTA's road AI cruise a vehicle indefinitely
// ##############################################################################

CTaskComplexCarDriveWanderSA::CTaskComplexCarDriveWanderSA(CVehicle* pTargetVehicle, float fSpeed, int iDrivingStyle) : CTaskComplexSA()
{
    CVehicleSA* pTargetVehicleSA = dynamic_cast<CVehicleSA*>(pTargetVehicle);

    if (pTargetVehicleSA)
    {
        CreateTaskInterface(sizeof(CTaskComplexCarDriveWanderSAInterface));
        if (!IsValid())
            return;

        DWORD dwFunc = FUNC_CTaskComplexCarDriveWander__Constructor;
        DWORD dwVehiclePtr = (DWORD)pTargetVehicleSA->GetInterface();
        DWORD dwThisInterface = (DWORD)GetInterface();

        // The verified native signature is (vehicle, drivingStyle, speed). Use
        // GTA's implementation so its safe-reference, autopilot setup and
        // destructor restoration remain intact.
        // clang-format off
        __asm
        {
            mov     ecx, dwThisInterface
            push    fSpeed
            push    iDrivingStyle
            push    dwVehiclePtr
            call    dwFunc
        }
        // clang-format on
    }
}

// ##############################################################################
// ## Name:    CTaskComplexCarDriveToPoint
// ## Purpose: Drives along GTA's road graph to a finite world point
// ##############################################################################

CTaskComplexCarDriveToPointSA::CTaskComplexCarDriveToPointSA(CVehicle* pTargetVehicle, const CVector& vecTarget, float fSpeed, int iDriveMode,
                                                             int iDesiredVehicleModel, float fRadius, int iDrivingStyle)
    : CTaskComplexSA()
{
    CVehicleSA* pTargetVehicleSA = pTargetVehicle ? dynamic_cast<CVehicleSA*>(pTargetVehicle) : nullptr;
    if (pTargetVehicle && !pTargetVehicleSA)
        return;

    CreateTaskInterface(sizeof(CTaskComplexCarDriveToPointSAInterface));
    if (!IsValid())
        return;

    DWORD dwFunc = FUNC_CTaskComplexCarDriveToPoint__Constructor;
    DWORD dwVehiclePtr = pTargetVehicleSA ? reinterpret_cast<DWORD>(pTargetVehicleSA->GetInterface()) : 0;
    DWORD dwTargetPtr = reinterpret_cast<DWORD>(&vecTarget);
    DWORD dwThisInterface = reinterpret_cast<DWORD>(GetInterface());

    // The null vehicle path is intentional for sequence children. GTA's car
    // drive base binds the ped's current vehicle when each child activates.
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    iDrivingStyle
        push    fRadius
        push    iDesiredVehicleModel
        push    iDriveMode
        push    fSpeed
        push    dwTargetPtr
        push    dwVehiclePtr
        call    dwFunc
    }
    // clang-format on
}

CTaskComplexCarDriveMissionSA::CTaskComplexCarDriveMissionSA(CVehicle* pVehicle, CEntity* pTarget, int iMission, int iDrivingStyle, float fSpeed)
{
    auto* vehicle = dynamic_cast<CVehicleSA*>(pVehicle);
    if (!vehicle || !pTarget || !pTarget->GetInterface())
        return;

    CreateTaskInterface(sizeof(CTaskComplexCarDriveMissionSAInterface));
    if (!IsValid())
        return;

    const DWORD dwThis = reinterpret_cast<DWORD>(GetInterface());
    const DWORD dwVehicle = reinterpret_cast<DWORD>(vehicle->GetInterface());
    const DWORD dwTarget = reinterpret_cast<DWORD>(pTarget->GetInterface());
    const DWORD dwFunc = FUNC_CTaskComplexCarDriveMission__Constructor;

    // GTA:SA 1.0 at 0x63CC30 consumes five stack arguments and returns with
    // RET 0x14. Calling the native constructor preserves its safe target
    // reference and restoration of the vehicle's previous autopilot mission.
    // clang-format off
    __asm
    {
        mov     ecx, dwThis
        push    fSpeed
        push    iDrivingStyle
        push    iMission
        push    dwTarget
        push    dwVehicle
        call    dwFunc
    }
    // clang-format on
}
