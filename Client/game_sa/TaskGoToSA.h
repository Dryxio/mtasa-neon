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
#define FUNC_CTaskComplexSeekEntityRadiusAngleOffset__Constructor 0x493730
#define FUNC_CTaskComplexSequence__Constructor                    0x632BD0
#define FUNC_CTaskComplexSequence__AddTask                        0x632D10
#define FUNC_CTaskComplexSequence__Flush                          0x632C10
#define FUNC_CTaskComplexUseSequence__Constructor                 0x635450
#define FUNC_CTaskSequences__GetAvailableSlot                     0x632E00

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

class CTaskComplexSeekEntityRadiusAngleOffsetSAInterface : public CTaskComplexSAInterface
{
private:
    unsigned char m_stateBeforeOffset[0x38];

public:
    float m_fRadius;
    float m_fAngleRadians;

private:
    unsigned char m_stateAfterOffset[0x8];
};
static_assert(sizeof(CTaskComplexSeekEntityRadiusAngleOffsetSAInterface) == 0x54, "Unexpected CTaskComplexSeekEntityRadiusAngleOffsetSAInterface size");

class CTaskComplexSeekEntityRadiusAngleOffsetSA : public virtual CTaskComplexSA, public virtual CTaskComplexSeekEntityRadiusAngleOffset
{
public:
    CTaskComplexSeekEntityRadiusAngleOffsetSA() {};
    CTaskComplexSeekEntityRadiusAngleOffsetSA(CPed* pTarget, int iTimeout, float fRadius, float fAngleDegrees);
};

class CTaskComplexSequenceSAInterface : public CTaskComplexSAInterface
{
public:
    int               m_iCurrentTask;
    CTaskSAInterface* m_pTasks[8];
    unsigned int      m_uiRepeatMode;
    int               m_iRepeatedCount;
    bool              m_bFlushTasks;
    unsigned char     m_ucPadding[3];
    unsigned int      m_uiReferenceCount;
};
static_assert(sizeof(CTaskComplexSequenceSAInterface) == 0x40, "Unexpected CTaskComplexSequenceSAInterface size");

class CTaskComplexSequenceSA : public virtual CTaskComplexSA
{
public:
    CTaskComplexSequenceSA() {};
};

class CTaskComplexUseSequenceSAInterface : public CTaskComplexSAInterface
{
public:
    int m_iSequenceIndex;
    int m_iCurrentTask;
    int m_iEndTask;
    int m_iRepeatedCount;
};
static_assert(sizeof(CTaskComplexUseSequenceSAInterface) == 0x1C, "Unexpected CTaskComplexUseSequenceSAInterface size");

class CTaskComplexUseSequenceSA : public virtual CTaskComplexSA
{
public:
    CTaskComplexUseSequenceSA() {};
    CTaskComplexUseSequenceSA(CTaskSA* pTask, bool bRepeat);
};
