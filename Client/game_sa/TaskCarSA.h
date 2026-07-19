/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskCarSA.h
 *  PURPOSE:     Car game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <game/TaskCar.h>
#include "CVehicleSA.h"
#include "TaskSA.h"

// temporary
class CAnimBlendAssociation;
typedef DWORD CTaskUtilityLineUpPedWithCar;
typedef DWORD AnimationId;

#define FUNC_CTaskComplexEnterCarAsDriver__Constructor    0x6402F0
#define FUNC_CTaskComplexEnterCarAsPassenger__Constructor 0x640340
#define FUNC_CTaskComplexEnterBoatAsDriver__Constructor   0x63B5E0
#define FUNC_CTaskComplexLeaveCar__Constructor            0x63B8C0
#define FUNC_CTaskComplexCarDriveWander__Constructor      0x63CB10
#define FUNC_CTaskComplexCarDriveToPoint__Constructor     0x63CE00

// ##############################################################################
// ## Name:    CTaskComplexEnterCar
// ## Purpose: Makes the ped enter the specified vehicle
// ## Notes:   Shouldn't be used directly, use CTaskComplexEnterCarAsDriver or
// ##          CTaskComplexEnterCarAsPassenger instead
// ##############################################################################

class CTaskComplexEnterCarSAInterface : public CTaskComplexSAInterface
{
public:
    CVehicle*     m_pTargetVehicle;
    unsigned char m_bAsDriver : 1;
    unsigned char m_bQuitAfterOpeningDoor : 1;
    unsigned char m_bQuitAfterDraggingPedOut : 1;
    unsigned char m_bCarryOnAfterFallingOff : 1;

    int m_iTargetDoor;
    int m_iTargetDoorOppositeToFlag;
    int m_iTargetSeat;
    int m_iDraggedPedDownTime;
    int m_iMoveState;

    unsigned char m_nNumGettingInSet;
    unsigned char m_nCamMovementChoice;

    CVector                       m_vTargetDoorPos;
    CTaskUtilityLineUpPedWithCar* m_pTaskUtilityLineUpPedWithCar;
    bool                          m_bIsAborting;
    CPed*                         m_pDraggedPed;
    unsigned char                 m_nDoorFlagsSet;
    float                         m_fCruiseSpeed;
    int                           m_iEnterCarStartTime;
};
// The native clone allocates exactly 0x50 bytes. Keeping this assertion beside
// the interface prevents later field edits from silently corrupting GTA's ctor.
static_assert(sizeof(CTaskComplexEnterCarSAInterface) == 0x50, "Invalid CTaskComplexEnterCarSAInterface size");

class CTaskComplexEnterCarSA : public virtual CTaskComplexSA
{
public:
    CTaskComplexEnterCarSA() {};
    CTaskComplexEnterCarSA(CVehicle* pTargetVehicle, const bool bAsDriver, const bool bQuitAfterOpeningDoor, const bool bQuitAfterDraggingPedOut,
                           const bool bCarryOnAfterFallingOff = false);

    int GetTargetDoor()
    {
        CTaskComplexEnterCarSAInterface* thisInterface = (CTaskComplexEnterCarSAInterface*)GetInterface();
        return thisInterface->m_iTargetDoor;
    }
    void SetTargetDoor(int iDoor)
    {
        CTaskComplexEnterCarSAInterface* thisInterface = (CTaskComplexEnterCarSAInterface*)GetInterface();
        thisInterface->m_iTargetDoor = iDoor;
    }

    int GetEnterCarStartTime()
    {
        CTaskComplexEnterCarSAInterface* thisInterface = (CTaskComplexEnterCarSAInterface*)GetInterface();
        return thisInterface->m_iEnterCarStartTime;
    }
};

// ##############################################################################
// ## Name:    CTaskComplexEnterCarAsDriver
// ## Purpose: Makes the ped enter the specified vehicle
// ##############################################################################

class CTaskComplexEnterCarAsDriverSAInterface : public CTaskComplexEnterCarSAInterface
{
public:
};

class CTaskComplexEnterCarAsDriverSA : public virtual CTaskComplexEnterCarSA, public virtual CTaskComplexEnterCarAsDriver
{
public:
    CTaskComplexEnterCarAsDriverSA() {};
    CTaskComplexEnterCarAsDriverSA(CVehicle* pTargetVehicle);

    // from CTaskComplexEnterCarSA
    int  GetTargetDoor() { return CTaskComplexEnterCarSA::GetTargetDoor(); };
    void SetTargetDoor(int iDoor) { return CTaskComplexEnterCarSA::SetTargetDoor(iDoor); };
    int  GetEnterCarStartTime() { return CTaskComplexEnterCarSA::GetEnterCarStartTime(); };
};

// ##############################################################################
// ## Name:    CTaskComplexEnterCarAsPassenger
// ## Purpose: Makes the ped enter the specified vehicle as a passenger
// ##############################################################################

class CTaskComplexEnterCarAsPassengerSAInterface : public CTaskComplexEnterCarSAInterface
{
public:
};
static_assert(sizeof(CTaskComplexEnterCarAsPassengerSAInterface) == 0x50, "Invalid CTaskComplexEnterCarAsPassengerSAInterface size");

class CTaskComplexEnterCarAsPassengerSA : public virtual CTaskComplexEnterCarSA, public virtual CTaskComplexEnterCarAsPassenger
{
public:
    CTaskComplexEnterCarAsPassengerSA() {};
    CTaskComplexEnterCarAsPassengerSA(CVehicle* pTargetVehicle, const int iTargetSeat = 0, const bool bCarryOnAfterFallingOff = false);

    // from CTaskComplexEnterCarSA
    int  GetTargetDoor() { return CTaskComplexEnterCarSA::GetTargetDoor(); };
    void SetTargetDoor(int iDoor) { return CTaskComplexEnterCarSA::SetTargetDoor(iDoor); };
    int  GetEnterCarStartTime() { return CTaskComplexEnterCarSA::GetEnterCarStartTime(); };
};

// ##############################################################################
// ## Name:    CTaskComplexEnterBoatAsDriver
// ## Purpose: Makes the ped enter the specified boat as the driver
// ##############################################################################

class CTaskComplexEnterBoatAsDriverSAInterface : public CTaskComplexSAInterface
{
public:
};

class CTaskComplexEnterBoatAsDriverSA : public virtual CTaskComplexSA, public virtual CTaskComplexEnterBoatAsDriver
{
public:
    CTaskComplexEnterBoatAsDriverSA() {};
    CTaskComplexEnterBoatAsDriverSA(CVehicle* pTargetVehicle);
};

// ##############################################################################
// ## Name:    CTaskComplexLeaveCar
// ## Purpose: Makes the ped leave a specific vehicle
// ##############################################################################

class CTaskComplexLeaveCarSAInterface : public CTaskComplexSAInterface
{
public:
    CVehicle* m_pTargetVehicle;
    int       m_iTargetDoor;
    int       m_iDelayTime;
    bool      m_bSensibleLeaveCar;
    bool      m_bForceGetOut;

    bool m_bDie;

    // GTA owns this helper and destroys it with the task. The original 0x34
    // layout stores it at +0x1C; omitting it shifts every trailing field and
    // makes the native constructor/destructor operate on the wrong offsets.
    CTaskUtilityLineUpPedWithCar* m_pTaskUtilityLineUpPedWithCar;

    unsigned char m_nDoorFlagsSet;
    unsigned char m_nNumGettingInSet;

    AnimationId m_dieAnim;
    float       m_fDieAnimBlendDelta;
    float       m_fDieAnimSpeed;

    unsigned char m_bIsInAir;
};
static_assert(sizeof(CTaskComplexLeaveCarSAInterface) == 0x34, "Invalid CTaskComplexLeaveCarSAInterface size");

class CTaskComplexLeaveCarSA : public virtual CTaskComplexSA, public virtual CTaskComplexLeaveCar
{
public:
    CTaskComplexLeaveCarSA() {};
    CTaskComplexLeaveCarSA(CVehicle* pTargetVehicle, const int iTargetDoor = 0xFF, const int iDelayTime = 0, const bool bSensibleLeaveCar = true,
                           const bool bForceGetOut = false);

    int GetTargetDoor()
    {
        CTaskComplexLeaveCarSAInterface* thisInterface = (CTaskComplexLeaveCarSAInterface*)GetInterface();
        return thisInterface->m_iTargetDoor;
    }
};

// ##############################################################################
// ## Name:    CTaskComplexCarDriveWander
// ## Purpose: Lets GTA's road AI cruise a vehicle indefinitely
// ##############################################################################

class CTaskComplexCarDriveWanderSAInterface : public CTaskComplexSAInterface
{
public:
    CVehicle* m_pTargetVehicle;
    float     m_fCruiseSpeed;
    int       m_iDesiredCarModel;
    int       m_iCarDrivingStyle;

    bool          m_bAsDriver;
    unsigned char m_nOriginalDrivingStyle;
    signed char   m_nOriginalMission;
    unsigned char m_nOriginalSpeed;
    bool          m_bIsCarSetUp;
};
// Handler 05D2 and the native clone both allocate exactly 0x24 bytes. The
// derived Wander task adds no fields to its car-drive base.
static_assert(sizeof(CTaskComplexCarDriveWanderSAInterface) == 0x24, "Invalid CTaskComplexCarDriveWanderSAInterface size");

class CTaskComplexCarDriveWanderSA : public virtual CTaskComplexSA, public virtual CTaskComplexCarDriveWander
{
public:
    CTaskComplexCarDriveWanderSA() {};
    CTaskComplexCarDriveWanderSA(CVehicle* pTargetVehicle, float fSpeed, int iDrivingStyle);
};

// ##############################################################################
// ## Name:    CTaskComplexCarDriveToPoint
// ## Purpose: Drives along GTA's road graph to a finite world point
// ##############################################################################

class CTaskComplexCarDriveToPointSAInterface : public CTaskComplexSAInterface
{
public:
    CVehicle* m_pTargetVehicle;
    float     m_fCruiseSpeed;
    int       m_iDesiredCarModel;
    int       m_iCarDrivingStyle;

    bool          m_bAsDriver;
    unsigned char m_nOriginalDrivingStyle;
    signed char   m_nOriginalMission;
    unsigned char m_nOriginalSpeed;
    bool          m_bIsCarSetUp;
    char          m_pad[3];

    CVector m_vecTargetPoint;
    int     m_iDriveMode;
    float   m_fTargetRadius;
    bool    m_bFinished;
};
// Opcode 05D1 allocates 0x3C bytes and the original constructor writes the
// target, mode, radius and completion flag at +0x24 through +0x38.
static_assert(sizeof(CTaskComplexCarDriveToPointSAInterface) == 0x3C, "Invalid CTaskComplexCarDriveToPointSAInterface size");

class CTaskComplexCarDriveToPointSA : public virtual CTaskComplexSA, public virtual CTaskComplexCarDriveToPoint
{
public:
    CTaskComplexCarDriveToPointSA() {};
    CTaskComplexCarDriveToPointSA(CVehicle* pTargetVehicle, const CVector& vecTarget, float fSpeed, int iDriveMode, int iDesiredVehicleModel, float fRadius,
                                  int iDrivingStyle);
};
