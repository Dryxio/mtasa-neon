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
class CPedSAInterface;
typedef DWORD CTaskUtilityLineUpPedWithCar;

#define FUNC_CTaskComplexWanderStandard__Constructor 0x48E4F0
#define FUNC_CTaskComplexWanderStandard__Destructor  0x48E600
#define FUNC_CTaskComplexWanderGang__Constructor     0x66F5C0
#define FUNC_CTaskComplexBeInGroup__Constructor      0x632E50
#define FUNC_CTaskComplexBeInCouple__Constructor     0x6836F0

#define FUNC_CTaskComplexWander__Constructor        0x66F450
#define FUNC_CTaskComplexWander__DeletingDestructor 0x66F4A0
#define FUNC_CTaskComplexWander__CreateNextSubTask  0x674140
#define FUNC_CTaskComplexWander__CreateFirstSubTask 0x6740E0
#define FUNC_CTaskComplexWander__ControlSubTask     0x674C30
#define FUNC_CTaskComplexWander__UpdateDir          0x669DA0
#define FUNC_CTaskComplexWander__UpdatePathNodes    0x669ED0
#define VTBL_CTaskComplexWander                     0x86FE84

#define FUNC_CTaskComplexGoToPointAndStandStill__Constructor      0x668120
#define FUNC_CTaskComplexGoToPointAndStandStillTimed__Constructor 0x6685E0
#define FUNC_CTaskComplexFollowNodeRoute__Constructor             0x66EA30
#define FUNC_CTaskComplexSeekEntityRadiusAngleOffset__Constructor 0x493730
#define FUNC_CTaskComplexTurnToFaceEntityOrCoord__Constructor     0x66B890
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
// ## Name:    CTaskSimpleGoTo
// ## Purpose: Common prefix used by GTA's point-navigation subtasks.
// ##############################################################################

class CTaskSimpleGoToSAInterface : public CTaskSimpleSAInterface
{
public:
    int m_iMoveState;
};
static_assert(offsetof(CTaskSimpleGoToSAInterface, m_iMoveState) == 0x08, "Invalid simple go-to movement offset");

class CTaskSimpleGoToSA : public virtual CTaskSimpleSA, public virtual CTaskSimpleGoTo
{
public:
    CTaskSimpleGoToSA() {};

    int GetMoveState() const override { return static_cast<const CTaskSimpleGoToSAInterface*>(GetInterface())->m_iMoveState; }
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
static_assert(offsetof(CTaskComplexWanderSAInterface, m_iMoveState) == 0x0C, "Invalid wander movement offset");
static_assert(sizeof(CTaskComplexWanderSAInterface) == 0x28, "Invalid wander task size");

class CTaskComplexWanderSA : public virtual CTaskComplexSA, public virtual CTaskComplexWander
{
public:
    CTaskComplexWanderSA() {};

    CNodeAddress* GetNextNode();
    CNodeAddress* GetLastNode();

    int GetWanderType();
    int GetMoveState() const override { return static_cast<const CTaskComplexWanderSAInterface*>(GetInterface())->m_iMoveState; }
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
static_assert(sizeof(CTaskComplexWanderStandardSAInterface) == 0x38, "Invalid standard wander task size");

class CTaskComplexWanderStandardSA : public virtual CTaskComplexWanderSA, public virtual CTaskComplexWanderStandard
{
public:
    CTaskComplexWanderStandardSA() {};
    CTaskComplexWanderStandardSA(const int iMoveState, const unsigned char iDir, const bool bWanderSensibly = true);
};

class CTaskComplexWanderGangSA : public virtual CTaskComplexWanderSA
{
public:
    CTaskComplexWanderGangSA(int moveState, unsigned char direction, unsigned int scanTime, bool wanderSensibly, float targetRadius);
};

// MTA peds are physically CPlayerPed instances, so GTA's retail WanderCop is
// unsafe: its first/next/control paths read CCopPed-only state and can enter
// wanted/pursuit code. This task keeps the retail Wander locomotion machine but
// supplies only the COP wander identity and an intentionally empty scanner.
class CTaskComplexWanderCopAmbientSA : public virtual CTaskComplexWanderSA
{
public:
    CTaskComplexWanderCopAmbientSA(int moveState, unsigned char direction);
};

TaskComplexWanderVTBL* GetTaskComplexWanderCopAmbientVTable() noexcept;
bool                   IsTaskComplexWanderCopAmbientInterface(const CTaskSAInterface* task) noexcept;
bool                   IsTaskComplexWanderCopAmbientVTableSafe() noexcept;

class CTaskComplexBeInGroupSA : public virtual CTaskComplexSA
{
public:
    CTaskComplexBeInGroupSA(int groupId, bool isLeader);
};

class CTaskComplexBeInCoupleSAInterface : public CTaskComplexSAInterface
{
public:
    std::uint32_t    m_opaqueWalkSide;
    CPedSAInterface* m_partner;
    bool             m_isLeader;
    bool             m_holdHands;
    bool             m_lookAtEachOther;
    unsigned char    m_padding17;
    float            m_giveUpDistance;
    unsigned char    m_previousSide;
    unsigned char    m_padding1D[3];
};
static_assert(offsetof(CTaskComplexBeInCoupleSAInterface, m_partner) == 0x10, "Invalid BeInCouple partner offset");
static_assert(offsetof(CTaskComplexBeInCoupleSAInterface, m_isLeader) == 0x14, "Invalid BeInCouple leader offset");
static_assert(offsetof(CTaskComplexBeInCoupleSAInterface, m_giveUpDistance) == 0x18, "Invalid BeInCouple distance offset");
static_assert(sizeof(CTaskComplexBeInCoupleSAInterface) == 0x20, "Invalid BeInCouple task size");

class CTaskComplexBeInCoupleSA : public virtual CTaskComplexSA
{
public:
    CTaskComplexBeInCoupleSA(CPed* partner, bool isLeader, bool holdHands = true, bool lookAtEachOther = true, float giveUpDistance = 10.0f);
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

    int GetMoveState() const override { return static_cast<const CTaskComplexGoToPointAndStandStillSAInterface*>(GetInterface())->m_iMoveState; }
};

class CTaskComplexFollowNodeRouteSAInterface : public CTaskComplexSAInterface
{
public:
    CVector      m_vecTarget;
    int          m_iMoveState;
    float        m_fTargetRadius;
    float        m_fSlowDownDistance;
    float        m_fHeightChangeThreshold;
    CNodeAddress m_StartNode;
    void*        m_pNodeRoute;
    void*        m_pPointRoute;
    CNodeAddress m_CurrentNode;
    unsigned int m_uiCurrentPoint;
    int          m_iTime;
    CTaskTimer   m_Timer;
    unsigned int m_uiFlags;
    float        m_fSpeedDecreaseDistance;
    float        m_fSpeedIncreaseDistance;
    float        m_fSpeedDecreaseAmount;
    float        m_fSpeedIncreaseAmount;
};
static_assert(offsetof(CTaskComplexFollowNodeRouteSAInterface, m_vecTarget) == 0x0C, "Invalid follow-node target offset");
static_assert(offsetof(CTaskComplexFollowNodeRouteSAInterface, m_iMoveState) == 0x18, "Invalid follow-node movement offset");
static_assert(offsetof(CTaskComplexFollowNodeRouteSAInterface, m_Timer) == 0x40, "Invalid follow-node timer offset");
static_assert(offsetof(CTaskComplexFollowNodeRouteSAInterface, m_uiFlags) == 0x4C, "Invalid follow-node flags offset");
static_assert(sizeof(CTaskComplexFollowNodeRouteSAInterface) == 0x60, "Invalid follow-node route task size");

class CTaskComplexFollowNodeRouteSA : public virtual CTaskComplexSA, public virtual CTaskComplexFollowNodeRoute
{
public:
    CTaskComplexFollowNodeRouteSA() {};
    CTaskComplexFollowNodeRouteSA(const int iMoveState, const CVector& vecTarget, const float fTargetRadius, const float fSlowDownDistance,
                                  const float fHeightChangeThreshold, const bool bKeepNodesHeadingAwayFromTarget, const int iTime, const bool bUseBlending);

    int GetMoveState() const override { return static_cast<const CTaskComplexFollowNodeRouteSAInterface*>(GetInterface())->m_iMoveState; }
};

class CTaskComplexGoToPointAndStandStillTimedSA : public virtual CTaskComplexSA, public virtual CTaskComplexGoToPointAndStandStill
{
public:
    CTaskComplexGoToPointAndStandStillTimedSA() {};
    CTaskComplexGoToPointAndStandStillTimedSA(const int iMoveState, const CVector& vecTarget, const float fTargetRadius, const float fSlowDownDistance,
                                              const int iTime);

    int GetMoveState() const override { return static_cast<const CTaskComplexGoToPointAndStandStillSAInterface*>(GetInterface())->m_iMoveState; }
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

class CTaskComplexTurnToFaceEntityOrCoordSAInterface : public CTaskComplexSAInterface
{
public:
    CEntitySAInterface* m_pEntityToFace;
    bool                m_bFaceEntity;
    unsigned char       m_ucPadding[3];
    CVector             m_vecCoordsToFace;
    float               m_fChangeRateMultiplier;
    float               m_fMaxHeading;
};
static_assert(sizeof(CTaskComplexTurnToFaceEntityOrCoordSAInterface) == 0x28, "Unexpected CTaskComplexTurnToFaceEntityOrCoordSAInterface size");
static_assert(offsetof(CTaskComplexTurnToFaceEntityOrCoordSAInterface, m_pEntityToFace) == 0x0C, "Invalid turn-to-face entity offset");
static_assert(offsetof(CTaskComplexTurnToFaceEntityOrCoordSAInterface, m_fChangeRateMultiplier) == 0x20, "Invalid turn-to-face change-rate offset");

class CTaskComplexTurnToFaceEntityOrCoordSA : public virtual CTaskComplexSA
{
public:
    CTaskComplexTurnToFaceEntityOrCoordSA() {};
    explicit CTaskComplexTurnToFaceEntityOrCoordSA(CPed* pTarget);
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
    CTaskComplexUseSequenceSA(CTaskSAInterface* const* pTasks, size_t uiTaskCount, bool bRepeat);

    int GetCurrentTaskIndex() const;

private:
    void Initialize(CTaskSAInterface* const* pTasks, size_t uiTaskCount, bool bRepeat);
};
