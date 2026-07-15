/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskGoToSA.h
 *  PURPOSE:     Go-to game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <CVector.h>
#include <game/TaskGoTo.h>
#include "CVehicleSA.h"
#include "TaskSA.h"

// temporary
class CAnimBlendAssociation;
typedef DWORD CTaskUtilityLineUpPedWithCar;

#define FUNC_CTaskComplexWanderStandard__Constructor 0x48E4F0
#define FUNC_CTaskComplexWanderStandard__Destructor  0x48E600

#define FUNC_CTaskComplexGoToPointAndStandStill__Constructor      0x668120
#define FUNC_CTaskComplexGoToPointAndStandStillTimed__Constructor 0x6685E0

#define FUNC_CTaskSimpleCarSetPedOut__PositionPedOutOfCollision 0x6479B0

class TaskComplexWanderVTBL : public TaskComplexVTBL
{
public:
    DWORD GetWanderType;
    DWORD ScanForStuff;
    DWORD UpdateDir;
    DWORD UpdatePathNodes;
};

// ##############################################################################
// ## Name:    CTaskComplexWander
// ## Purpose: Generic task that makes peds wander around. Can't be used
// ##          directly, use a superclass of this instead.
// ##############################################################################

class CTaskComplexWanderSAInterface : public CTaskComplexSAInterface
{
public:
    // protected
    int           m_iMoveState;
    unsigned char m_iDir;
    float         m_targetRadius;

    CNodeAddress m_LastNode;
    CNodeAddress m_NextNode;

    int m_lastUpdateDirFrameCount;

    unsigned char m_bWanderSensibly : 1;
    // private
    unsigned char m_bNewDir : 1;
    unsigned char m_bNewNodes : 1;
    unsigned char m_bAllNodesBlocked : 1;
};

class CTaskComplexWanderSA : public virtual CTaskComplexSA, public virtual CTaskComplexWander
{
public:
    CTaskComplexWanderSA() {};

    CNodeAddress* GetNextNode();
    CNodeAddress* GetLastNode();

    int GetWanderType();
};

// ##############################################################################
// ## Name:    CTaskComplexWanderStandard
// ## Purpose: Standard class used for making normal peds wander around
// ##############################################################################

class CTaskComplexWanderStandardSAInterface : public CTaskComplexWanderSAInterface
{
public:
    // private
    CTaskTimer m_timer;
    int        m_iMinNextScanTime;
};

class CTaskComplexWanderStandardSA : public virtual CTaskComplexWanderSA, public virtual CTaskComplexWanderStandard
{
public:
    CTaskComplexWanderStandardSA() {};
    CTaskComplexWanderStandardSA(const int iMoveState, const unsigned char iDir, const bool bWanderSensibly = true);
};

// Keep these layouts explicit: GTA's constructors initialise memory allocated by
// MTA, so allocating even one byte too little would corrupt the adjacent heap data.
class CTaskComplexGoToPointAndStandStillSAInterface : public CTaskComplexSAInterface
{
public:
    int           m_iMoveState;
    CVector       m_vecTarget;
    float         m_fTargetRadius;
    float         m_fSlowDownDistance;
    unsigned char m_ucFlags;
    unsigned char m_ucPadding[3];
};
static_assert(sizeof(CTaskComplexGoToPointAndStandStillSAInterface) == 0x28, "Invalid CTaskComplexGoToPointAndStandStillSAInterface size");

class CTaskComplexGoToPointAndStandStillTimedSAInterface : public CTaskComplexGoToPointAndStandStillSAInterface
{
public:
    int        m_iTime;
    CTaskTimer m_Timer;
};
static_assert(sizeof(CTaskComplexGoToPointAndStandStillTimedSAInterface) == 0x38, "Invalid CTaskComplexGoToPointAndStandStillTimedSAInterface size");

class CTaskComplexGoToPointAndStandStillSA : public virtual CTaskComplexSA, public virtual CTaskComplexGoToPointAndStandStill
{
public:
    CTaskComplexGoToPointAndStandStillSA() {};
    CTaskComplexGoToPointAndStandStillSA(const int iMoveState, const CVector& vecTarget, const float fTargetRadius, const float fSlowDownDistance);
};

class CTaskComplexGoToPointAndStandStillTimedSA : public virtual CTaskComplexSA, public virtual CTaskComplexGoToPointAndStandStill
{
public:
    CTaskComplexGoToPointAndStandStillTimedSA() {};
    CTaskComplexGoToPointAndStandStillTimedSA(const int iMoveState, const CVector& vecTarget, const float fTargetRadius, const float fSlowDownDistance,
                                              const int iTime);
};
