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
