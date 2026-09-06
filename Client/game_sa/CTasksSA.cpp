/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/game_sa/CTasksSA.cpp
 *  PURPOSE:     Task creation
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CGameSA.h"
#include "CPedSA.h"
#include "CTaskManagementSystemSA.h"
#include "CTaskManagerSA.h"
#include "CTasksSA.h"
#include "TaskAttackSA.h"
#include "TaskBasicSA.h"
#include "TaskCarAccessoriesSA.h"
#include "TaskCarSA.h"
#include "TaskGoToSA.h"
#include "TaskIKSA.h"
#include "TaskJumpFallSA.h"
#include "TaskPhysicalResponseSA.h"
#include "TaskSA.h"
#include "TaskSecondarySA.h"
#include "CAnimManagerSA.h"
#include "CObjectSA.h"
#include "CPedIntelligenceSA.h"

extern CGameSA* pGame;

namespace
{
    constexpr std::uintptr_t FUNC_CPedIntelligence_AddTaskPrimaryMaybeInGroup = 0x600E20;
}

CTasksSA::CTasksSA(CTaskManagementSystemSA* pTaskManagementSystem)
{
    m_pTaskManagementSystem = pTaskManagementSystem;
}

bool CTasksSA::AddPedScriptCommandTask(CPed* pPed, CTask* pTask, bool bAffectsDeadPeds)
{
    CPedSAInterface*  pPedInterface = pPed ? pPed->GetPedInterface() : nullptr;
    CTaskSAInterface* pTaskInterface = pTask ? pTask->GetInterface() : nullptr;
    if (!pPedInterface || !pPedInterface->pPedIntelligence || !pTaskInterface || pTaskInterface->m_pParent)
        return false;

    // GTA's own helper routes the fresh task through CEventScriptCommand for
    // players and ungrouped peds. That event path clears competing event-response
    // tasks before installing its clone, which direct CTaskManager assignment
    // cannot reproduce. The helper destroys the original task before returning,
    // including when the native event queue is full, so none of these pointers
    // may be accessed after this call.
    using AddTaskPrimaryMaybeInGroup = void(__thiscall*)(CPedIntelligenceSAInterface*, CTaskSAInterface*, bool);
    reinterpret_cast<AddTaskPrimaryMaybeInGroup>(FUNC_CPedIntelligence_AddTaskPrimaryMaybeInGroup)(pPedInterface->pPedIntelligence, pTaskInterface,
                                                                                                   bAffectsDeadPeds);
    return true;
}

bool CTasksSA::IsPedScriptCommandTaskReady(CPed* pPed) const
{
    const CPedSAInterface* pPedInterface = pPed ? pPed->GetPedInterface() : nullptr;
    return pPedInterface && pPedInterface->pPedIntelligence;
}

CTaskSimplePlayerOnFoot* CTasksSA::CreateTaskSimplePlayerOnFoot()
{
    CTaskSimplePlayerOnFootSA* pTask = NewTask<CTaskSimplePlayerOnFootSA>();
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexFacial* CTasksSA::CreateTaskComplexFacial()
{
    CTaskComplexFacialSA* pTask = NewTask<CTaskComplexFacialSA>();
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleCarSetPedInAsDriver* CTasksSA::CreateTaskSimpleCarSetPedInAsDriver(CVehicle* pVehicle)
{
    CTaskSimpleCarSetPedInAsDriverSA* pTask = NewTask<CTaskSimpleCarSetPedInAsDriverSA>(pVehicle, (CTaskUtilityLineUpPedWithCar*)NULL);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleCarSetPedInAsPassenger* CTasksSA::CreateTaskSimpleCarSetPedInAsPassenger(CVehicle* pVehicle, int iTargetDoor)
{
    CTaskSimpleCarSetPedInAsPassengerSA* pTask = NewTask<CTaskSimpleCarSetPedInAsPassengerSA>(pVehicle, iTargetDoor, (CTaskUtilityLineUpPedWithCar*)NULL);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleCarSetPedOut* CTasksSA::CreateTaskSimpleCarSetPedOut(CVehicle* pVehicle, int iTargetDoor, bool bSwitchOffEngine)
{
    CTaskSimpleCarSetPedOutSA* pTask = NewTask<CTaskSimpleCarSetPedOutSA>(pVehicle, iTargetDoor, bSwitchOffEngine);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleBikeJacked* CTasksSA::CreateTaskSimpleBikeJacked(CVehicle* pVehicle, int iDoor, int iDraggedPedDownTime, CPed* pJacker, bool bVictimIsDriver)
{
    auto* pTask = NewTask<CTaskSimpleBikeJackedSA>(pVehicle, iDoor, iDraggedPedDownTime, pJacker, bVictimIsDriver);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexWanderStandard* CTasksSA::CreateTaskComplexWanderStandard(const int iMoveState, const char iDir, const bool bWanderSensibly)
{
    const char                    iNativeDir = iDir < 0 ? static_cast<char>(((int(__cdecl*)(int, int))0x407180)(0, 8)) : iDir;
    CTaskComplexWanderStandardSA* pTask = NewTask<CTaskComplexWanderStandardSA>(iMoveState, iNativeDir, bWanderSensibly);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexWander* CTasksSA::CreateTaskComplexWanderCopAmbient(const int iMoveState, const char iDir)
{
    const char iNativeDir = iDir < 0 ? static_cast<char>(((int(__cdecl*)(int, int))0x407180)(0, 8)) : iDir;
    if (!IsTaskComplexWanderCopAmbientVTableSafe())
        return nullptr;

    auto* pTask = NewTask<CTaskComplexWanderCopAmbientSA>(iMoveState, iNativeDir);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

bool CTasksSA::IsTaskComplexWanderCopAmbient(const CTask* pTask) const
{
    // CTask already exposes its native interface. Avoid RTTI across the SDK /
    // Game SA module boundary: the live wrapper may be viewed through a
    // different virtual base even though it owns the exact ambient vtable.
    return pTask && IsTaskComplexWanderCopAmbientVTableSafe() && IsTaskComplexWanderCopAmbientInterface(pTask->GetInterface());
}

CTaskComplexGoToPointAndStandStill* CTasksSA::CreateTaskComplexGoToPointAndStandStill(const int iMoveState, const CVector& vecTarget, const float fTargetRadius,
                                                                                      const float fSlowDownDistance, const int iTime)
{
    // SCM uses -2 to request the non-timed native task. Its ordinary default
    // timeout (-1) is normalised to 20 seconds before constructing GTA's timed
    // variant, matching the verified 05D3 command path.
    CTaskComplexGoToPointAndStandStill* pTask = nullptr;
    if (iTime == -2)
    {
        pTask = NewTask<CTaskComplexGoToPointAndStandStillSA>(iMoveState, vecTarget, fTargetRadius, fSlowDownDistance);
    }
    else
    {
        const int iNativeTime = iTime == -1 ? 20000 : iTime;
        pTask = NewTask<CTaskComplexGoToPointAndStandStillTimedSA>(iMoveState, vecTarget, fTargetRadius, fSlowDownDistance, iNativeTime);
    }

    m_pTaskManagementSystem->AddTask(dynamic_cast<CTaskSA*>(pTask));
    return pTask;
}

CTaskComplexFollowNodeRoute* CTasksSA::CreateTaskComplexFollowNodeRoute(const int iMoveState, const CVector& vecTarget, const float fTargetRadius,
                                                                        const float fSlowDownDistance, const float fHeightChangeThreshold,
                                                                        const bool bKeepNodesHeadingAwayFromTarget, const int iTime, const bool bUseBlending)
{
    auto* pTask = NewTask<CTaskComplexFollowNodeRouteSA>(iMoveState, vecTarget, fTargetRadius, fSlowDownDistance, fHeightChangeThreshold,
                                                         bKeepNodesHeadingAwayFromTarget, iTime, bUseBlending);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexEnterCarAsDriver* CTasksSA::CreateTaskComplexEnterCarAsDriver(CVehicle* pVehicle)
{
    CTaskComplexEnterCarAsDriverSA* pTask = NewTask<CTaskComplexEnterCarAsDriverSA>(pVehicle);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexEnterCarAsPassenger* CTasksSA::CreateTaskComplexEnterCarAsPassenger(CVehicle* pVehicle, const int iTargetSeat, const bool bCarryOnAfterFallingOff)
{
    CTaskComplexEnterCarAsPassengerSA* pTask = NewTask<CTaskComplexEnterCarAsPassengerSA>(pVehicle, iTargetSeat, bCarryOnAfterFallingOff);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexEnterBoatAsDriver* CTasksSA::CreateTaskComplexEnterBoatAsDriver(CVehicle* pVehicle)
{
    CTaskComplexEnterBoatAsDriverSA* pTask = NewTask<CTaskComplexEnterBoatAsDriverSA>(pVehicle);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexLeaveCar* CTasksSA::CreateTaskComplexLeaveCar(CVehicle* pVehicle, const int iTargetDoor, const int iDelayTime, const bool bSensibleLeaveCar,
                                                          const bool bForceGetOut)
{
    CTaskComplexLeaveCarSA* pTask = NewTask<CTaskComplexLeaveCarSA>(pVehicle, iTargetDoor, iDelayTime, bSensibleLeaveCar, bForceGetOut);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexCarDriveWander* CTasksSA::CreateTaskComplexCarDriveWander(CVehicle* pVehicle, float fSpeed, int iDrivingStyle)
{
    CTaskComplexCarDriveWanderSA* pTask = NewTask<CTaskComplexCarDriveWanderSA>(pVehicle, fSpeed, iDrivingStyle);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexUseMobilePhone* CTasksSA::CreateTaskComplexUseMobilePhone(const int iDuration)
{
    CTaskComplexUseMobilePhoneSA* pTask = NewTask<CTaskComplexUseMobilePhoneSA>(iDuration);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleDuck* CTasksSA::CreateTaskSimpleDuck(eDuckControlTypes nDuckControl, unsigned short nLengthOfDuck, unsigned short nUseShotsWhizzingEvents)
{
    CTaskSimpleDuckSA* pTask = NewTask<CTaskSimpleDuckSA>(nDuckControl, nLengthOfDuck, nUseShotsWhizzingEvents);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleChoking* CTasksSA::CreateTaskSimpleChoking(CPed* pAttacker, bool bIsTearGas)
{
    CTaskSimpleChokingSA* pTask = NewTask<CTaskSimpleChokingSA>(pAttacker, bIsTearGas);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleClimb* CTasksSA::CreateTaskSimpleClimb(CEntitySAInterface* pClimbEnt, const CVector& vecTarget, float fHeading, unsigned char nSurfaceType,
                                                  eClimbHeights nHeight, const bool bForceClimb)
{
    CTaskSimpleClimbSA* pTask = NewTask<CTaskSimpleClimbSA>(pClimbEnt, vecTarget, fHeading, nSurfaceType, nHeight, bForceClimb);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleJetPack* CTasksSA::CreateTaskSimpleJetpack(const CVector* pVecTargetPos, float fCruiseHeight, int nHoverTime)
{
    CTaskSimpleJetPackSA* pTask = NewTask<CTaskSimpleJetPackSA>(pVecTargetPos, fCruiseHeight, nHoverTime);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleRunAnim* CTasksSA::CreateTaskSimpleRunAnim(const AssocGroupId animGroup, const AnimationId animID, const float fBlendDelta, const int iTaskType,
                                                      const char* pTaskName, const bool bHoldLastFrame)
{
    if (!pGame->GetAnimManager()->IsValidAnim(animGroup, animID))
        return nullptr;

    CTaskSimpleRunAnimSA* pTask = NewTask<CTaskSimpleRunAnimSA>(animGroup, animID, fBlendDelta, iTaskType, pTaskName, bHoldLastFrame);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleRunNamedAnim* CTasksSA::CreateTaskSimpleRunNamedAnim(const char* pAnimName, const char* pAnimGroupName, const int flags, const float fBlendDelta,
                                                                const int iTime, const bool bDontInterrupt, const bool bRunInSequence, const bool bOffsetPed,
                                                                const bool bHoldLastFrame)
{
    CTaskSimpleRunNamedAnimSA* pTask =
        NewTask<CTaskSimpleRunNamedAnimSA>(pAnimName, pAnimGroupName, flags, fBlendDelta, iTime, bDontInterrupt, bRunInSequence, bOffsetPed, bHoldLastFrame);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexInWater* CTasksSA::CreateTaskComplexInWater()
{
    CTaskComplexInWaterSA* task = NewTask<CTaskComplexInWaterSA>();
    m_pTaskManagementSystem->AddTask(task);
    return task;
}

CTaskComplexDie* CTasksSA::CreateTaskComplexDie(const eWeaponType eMeansOfDeath, const AssocGroupId animGroup, const AnimationId anim, const float fBlendDelta,
                                                const float fAnimSpeed, const bool bBeingKilledByStealth, const bool bFallingToDeath, const int iFallToDeathDir,
                                                const bool bFallToDeathOverRailing)
{
    if (!pGame->GetAnimManager()->IsValidAnim(animGroup, anim))
        return nullptr;

    CTaskComplexDieSA* pTask = NewTask<CTaskComplexDieSA>(eMeansOfDeath, animGroup, anim, fBlendDelta, fAnimSpeed, bBeingKilledByStealth, bFallingToDeath,
                                                          iFallToDeathDir, bFallToDeathOverRailing);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleStealthKill* CTasksSA::CreateTaskSimpleStealthKill(bool bKiller, class CPed* pPed, const AnimationId animGroup)
{
    if (!pGame->GetAnimManager()->IsValidGroup(animGroup))
        return nullptr;

    CTaskSimpleStealthKillSA* pTask = NewTask<CTaskSimpleStealthKillSA>(bKiller, pPed, animGroup);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleDead* CTasksSA::CreateTaskSimpleDead(unsigned int uiDeathTimeMS, bool bUnk)
{
    CTaskSimpleDeadSA* pTask = NewTask<CTaskSimpleDeadSA>(uiDeathTimeMS, bUnk);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleBeHit* CTasksSA::CreateTaskSimpleBeHit(CPed* pPedAttacker, ePedPieceTypes hitBodyPart, int hitBodySide, int weaponId)
{
    CTaskSimpleBeHitSA* pTask = NewTask<CTaskSimpleBeHitSA>(pPedAttacker, hitBodyPart, hitBodySide, weaponId);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexSunbathe* CTasksSA::CreateTaskComplexSunbathe(class CObject* pTowel, const bool bStartStanding)
{
    CTaskComplexSunbatheSA* pTask = NewTask<CTaskComplexSunbatheSA>(pTowel, bStartStanding);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleIKChain* CTasksSA::CreateTaskSimpleIKChain(char* idString, int effectorBoneTag, CVector effectorVec, int pivotBoneTag, CEntity* pEntity,
                                                      int offsetBoneTag, CVector offsetPos, float speed, int time, int blendTime)
{
    CTaskSimpleIKChainSA* pTask =
        NewTask<CTaskSimpleIKChainSA>(idString, effectorBoneTag, effectorVec, pivotBoneTag, pEntity, offsetBoneTag, offsetPos, speed, time, blendTime);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleIKLookAt* CTasksSA::CreateTaskSimpleIKLookAt(char* idString, CEntity* pEntity, int time, int offsetBoneTag, CVector offsetPos,
                                                        unsigned char useTorso, float speed, int blendTime, int m_priority)
{
    CTaskSimpleIKLookAtSA* pTask = NewTask<CTaskSimpleIKLookAtSA>(idString, pEntity, time, offsetBoneTag, offsetPos, useTorso, speed, blendTime, m_priority);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleTriggerLookAt* CTasksSA::CreateTaskSimpleTriggerLookAt(CEntity* pEntity, int time, int offsetBoneTag, CVector offsetPos, unsigned char useTorso,
                                                                  float speed, int blendTime, int priority)
{
    CTaskSimpleTriggerLookAtSA* pTask = NewTask<CTaskSimpleTriggerLookAtSA>(pEntity, time, offsetBoneTag, offsetPos, useTorso, speed, blendTime, priority);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleGangDriveBy* CTasksSA::CreateTaskSimpleGangDriveBy(CEntity* pTargetEntity, const CVector* pVecTarget, float fAbortRange, char FrequencyPercentage,
                                                              char nDrivebyStyle, bool bSeatRHS)
{
    CTaskSimpleGangDriveBySA* pTask = NewTask<CTaskSimpleGangDriveBySA>(pTargetEntity, pVecTarget, fAbortRange, FrequencyPercentage, nDrivebyStyle, bSeatRHS);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleUseGun* CTasksSA::CreateTaskSimpleUseGun(CEntity* pTargetEntity, CVector vecTarget, char nCommand, short nBurstLength, unsigned char bAimImmediate)
{
    CTaskSimpleUseGunSA* pTask = NewTask<CTaskSimpleUseGunSA>(pTargetEntity, vecTarget, nCommand, nBurstLength, bAimImmediate);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleGunControl* CTasksSA::CreateTaskSimpleGunControl(CEntity* pTargetEntity, const CVector* pVecTarget, const CVector* pVecMoveTarget, char nFiringTask,
                                                            short nBurstLength, int iDuration)
{
    CTaskSimpleGunControlSA* pTask = NewTask<CTaskSimpleGunControlSA>(pTargetEntity, pVecTarget, pVecMoveTarget, nFiringTask, nBurstLength, iDuration);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleFight* CTasksSA::CreateTaskSimpleFight(CEntity* pTargetEntity, int nCommand, unsigned int nIdlePeriod)
{
    CTaskSimpleFightSA* pTask = NewTask<CTaskSimpleFightSA>(pTargetEntity, nCommand, nIdlePeriod);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexPartnerChat* CTasksSA::CreateTaskComplexPartnerChat(CPed* pPartner, bool bLeadSpeaker, bool bUpdateDirection)
{
    auto* pTask = NewTask<CTaskComplexPartnerChatSA>(pPartner, bLeadSpeaker, bUpdateDirection, true);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexPartnerChat* CTasksSA::CreateTaskComplexPartnerChatEx(CPed* pPartner, bool bLeadSpeaker, bool bUpdateDirection, bool bConversationEnabled)
{
    auto* pTask = NewTask<CTaskComplexPartnerChatSA>(pPartner, bLeadSpeaker, bUpdateDirection, bConversationEnabled);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplex* CTasksSA::CreateTaskComplexTurnToFaceEntity(CPed* pTarget)
{
    auto* pTask = NewTask<CTaskComplexTurnToFaceEntityOrCoordSA>(pTarget);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplex* CTasksSA::CreateTaskComplexSequence(CTask* const* pTasks, size_t uiTaskCount, bool bRepeat)
{
    CTaskSA*          taskWrappers[8] = {};
    CTaskSAInterface* taskInterfaces[8] = {};
    bool              valid = pTasks && uiTaskCount > 0 && uiTaskCount <= 8;

    if (valid)
    {
        for (size_t i = 0; i < uiTaskCount; ++i)
        {
            taskWrappers[i] = dynamic_cast<CTaskSA*>(pTasks[i]);
            if (!taskWrappers[i] || !taskWrappers[i]->IsValid() || taskWrappers[i]->GetInterface()->m_pParent ||
                m_pTaskManagementSystem->GetTask(taskWrappers[i]->GetInterface()) != taskWrappers[i])
            {
                valid = false;
                break;
            }
        }
    }

    if (!valid)
    {
        if (pTasks)
        {
            for (size_t i = 0; i < uiTaskCount && i < 8; ++i)
            {
                auto* pTask = dynamic_cast<CTaskSA*>(pTasks[i]);
                if (pTask && pTask->IsValid())
                    pTask->Destroy();
                else
                    delete pTask;
            }
        }
        return nullptr;
    }

    for (size_t i = 0; i < uiTaskCount; ++i)
        taskInterfaces[i] = m_pTaskManagementSystem->TakeTaskInterface(taskWrappers[i]);

    auto* pSequence = NewTask<CTaskComplexUseSequenceSA>(taskInterfaces, uiTaskCount, bRepeat);
    m_pTaskManagementSystem->AddTask(pSequence);
    return pSequence;
}

int CTasksSA::GetTaskSequenceProgress(CPed* pPed)
{
    if (!pPed || !pPed->GetPedIntelligence())
        return -1;

    CTaskManager* pTaskManager = pPed->GetPedIntelligence()->GetTaskManager();
    if (!pTaskManager)
        return -1;

    for (int priority = TASK_PRIORITY_PHYSICAL_RESPONSE; priority < TASK_PRIORITY_MAX; ++priority)
    {
        auto* pTask = dynamic_cast<CTaskComplexUseSequenceSA*>(pTaskManager->FindTaskByType(priority, TASK_COMPLEX_USE_SEQUENCE));
        if (pTask)
            return pTask->GetCurrentTaskIndex();
    }

    return -1;
}

CTaskComplexCarDriveToPoint* CTasksSA::CreateTaskComplexCarDriveToPoint(CVehicle* pVehicle, const CVector& vecTarget, float fSpeed, int iDriveMode,
                                                                        int iDesiredVehicleModel, float fRadius, int iDrivingStyle)
{
    auto* pTask = NewTask<CTaskComplexCarDriveToPointSA>(pVehicle, vecTarget, fSpeed, iDriveMode, iDesiredVehicleModel, fRadius, iDrivingStyle);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplex* CTasksSA::CreateTaskComplexCarDriveMission(CVehicle* pVehicle, CEntity* pTarget, int iMission, int iDrivingStyle, float fSpeed)
{
    auto* pTask = NewTask<CTaskComplexCarDriveMissionSA>(pVehicle, pTarget, iMission, iDrivingStyle, fSpeed);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplex* CTasksSA::CreateTaskComplexSmartFleeEntity(CPed* pTarget, bool bScream, float fSafeDistance, int iDuration, int iPositionCheckPeriod,
                                                         float fPositionChangeTolerance)
{
    auto* pTask = NewTask<CTaskComplexSmartFleeEntitySA>(pTarget, bScream, fSafeDistance, iDuration, iPositionCheckPeriod, fPositionChangeTolerance);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplex* CTasksSA::CreateTaskComplexJump(bool bAllowClimb)
{
    auto* pTask = NewTask<CTaskComplexJumpSA>(bAllowClimb);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplex* CTasksSA::CreateTaskComplexInAirAndLand(bool bUsingJumpGlide, bool bUsingFallGlide)
{
    auto* pTask = NewTask<CTaskComplexInAirAndLandSA>(bUsingJumpGlide, bUsingFallGlide);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplex* CTasksSA::CreateTaskSimpleClimbTakeover(CPed* pPed, const SClimbTaskState& state)
{
    CEntitySAInterface* pClimbEnt = CTaskSimpleClimbSA::ResolveTakeoverAnchor(pPed, state);
    if (!pClimbEnt)
        return nullptr;

    // Always construct GRAB first. GTA's PULLUP branch assumes an existing
    // animation association and dereferences it before creating CLIMB_PULL.
    auto* climbTask = NewTask<CTaskSimpleClimbSA>(pClimbEnt, state.handhold, state.handholdHeading, state.surfaceType, CLIMB_GRAB, state.forceClimb);
    if (!climbTask || !climbTask->PrepareTakeoverState(pPed, state))
    {
        if (climbTask)
            climbTask->Destroy();
        return nullptr;
    }

    auto* rootTask = NewTask<CTaskComplexJumpSA>(state.forceClimb ? 1 : 0);
    if (!rootTask)
    {
        climbTask->Destroy();
        return nullptr;
    }

    // The leaf cannot safely live directly in CTaskManager: on abort or an
    // invalid anchor, CTaskComplexJump consumes its invalid-climb state and
    // continues through InAirAndLand. Preserve that native parent lifecycle
    // while replacing only the already-completed launch subtask.
    m_pTaskManagementSystem->AddTask(climbTask);
    m_pTaskManagementSystem->AddTask(rootTask);
    rootTask->SetSubTask(climbTask);
    return rootTask;
}

CTaskComplex* CTasksSA::CreateTaskComplexJumpForScriptPed(bool bAllowClimb)
{
    auto* pTask = NewTask<CTaskComplexJumpSA>(bAllowClimb ? 1 : -1);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskSimpleStandStill* CTasksSA::CreateTaskSimpleStandStill(int iDuration)
{
    auto* pTask = NewTask<CTaskSimpleStandStillSA>(iDuration);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplex* CTasksSA::CreateTaskComplexGoToEntityOffset(CPed* pTarget, int iTimeout, float fRadius, float fAngleDegrees, bool bRepeat)
{
    CTaskComplexSA* pTask = nullptr;
    if (bRepeat)
    {
        auto* pSeekTask = NewTask<CTaskComplexSeekEntityRadiusAngleOffsetSA>(pTarget, iTimeout, fRadius, fAngleDegrees);
        auto* pUseSequenceTask = pSeekTask ? NewTask<CTaskComplexUseSequenceSA>(pSeekTask, true) : nullptr;
        if (pUseSequenceTask && !pUseSequenceTask->IsValid())
        {
            delete pUseSequenceTask;
            pUseSequenceTask = nullptr;
        }
        pTask = pUseSequenceTask;
    }
    else
    {
        pTask = NewTask<CTaskComplexSeekEntityRadiusAngleOffsetSA>(pTarget, iTimeout, fRadius, fAngleDegrees);
    }

    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

CTaskComplexKillPedOnFoot* CTasksSA::CreateTaskComplexKillPedOnFoot(CPed* pTarget)
{
    auto* pTask = NewTask<CTaskComplexKillPedOnFootSA>(pTarget);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
}

////////////////////////////////////////////////////////////////
//
// CEventHandler_ComputeDamageResponse_Mid
//
// Detect when GTA will start the 'be hit' task
//
////////////////////////////////////////////////////////////////
__declspec(noinline) void _cdecl OnCEventHandler_ComputeDamageResponse_Mid(CPedSAInterface* pPedVictim, CPedSAInterface* pPedAttacker,
                                                                           ePedPieceTypes hitBodyPart, int hitBodySide, int weaponId)
{
    // Make sure victim is local player
    CPedSAInterface* pLocalPlayer = ((CPoolsSA*)pGame->GetPools())->GetPedInterface((DWORD)1);
    if (pPedVictim != pLocalPlayer)
        return;

    if (pGame->m_pTaskSimpleBeHitHandler)
        pGame->m_pTaskSimpleBeHitHandler(pPedAttacker, hitBodyPart, hitBodySide, weaponId);
}

// Hook info
#define HOOKPOS_CEventHandler_ComputeDamageResponse_Mid  0x4C0593
#define HOOKSIZE_CEventHandler_ComputeDamageResponse_Mid 5
DWORD                         RETURN_CEventHandler_ComputeDamageResponse_Mid = 0x4C0598;
DWORD                         CTaskSimpleBeHit_constructor = FUNC_CTaskSimpleBeHit__Constructor;
static void __declspec(naked) HOOK_CEventHandler_ComputeDamageResponse_Mid()
{
    MTA_VERIFY_HOOK_LOCAL_SIZE;

    // clang-format off
    __asm
    {
        pushad
        push    [esp+32+4*3]
        push    [esp+32+4*3]
        push    [esp+32+4*3]
        push    [esp+32+4*3]
        push    [edi]
        call    OnCEventHandler_ComputeDamageResponse_Mid
        add     esp, 4*4+4
        popad

        // Replaced code
        call    CTaskSimpleBeHit_constructor
        jmp     RETURN_CEventHandler_ComputeDamageResponse_Mid
    }
    // clang-format on
}

//////////////////////////////////////////////////////////////////////////////////////////
//
// Setup hooks
//
//////////////////////////////////////////////////////////////////////////////////////////
void CTasksSA::StaticSetHooks()
{
    EZHookInstall(CEventHandler_ComputeDamageResponse_Mid);
    InstallTaskCarSAHooks();
}

namespace
{
    // Retail 1.0 US layout. Keep the animation implementation in GTA, while
    // releasing its entity reference before it can destroy an MTA-owned object.
    struct SCargoTask : CTaskSimpleSAInterface
    {
        CEntitySAInterface* entity;
        CVector             offset;
        unsigned char       bone, flags, padding[2];
        float               rotation;
        int                 animation, group, animationFlags;
        void*               block;
        void*               hierarchy;
        bool                dropped, needsProcessing, disallowDrop, padding2;
        void*               association;
    };
    static_assert(sizeof(SCargoTask) == 0x3C, "Retail cargo task layout changed");
    static_assert(offsetof(SCargoTask, entity) == 8, "Retail cargo reference offset changed");
    static_assert(offsetof(SCargoTask, dropped) == 0x34, "Retail cargo state offset changed");

    struct SCargoLease
    {
        CObject*            object;
        CObjectSAInterface* native;
        bool                collision, visible, streaming, attached, stationary, movingList;
        unsigned char       objectType;
    };
    std::map<CPed*, SCargoLease> cargoLeases;
    constexpr DWORD              cargoTables[] = {0x870B2C, 0x870B50, 0x870B74};
    DWORD                        cargoDestructors[3]{};

    int CargoKind(CTaskSAInterface* task)
    {
        for (int i = 0; task && i < 3; ++i)
            if (reinterpret_cast<DWORD>(task->VTBL) == cargoTables[i])
                return i;
        return -1;
    }

    void ReleaseCargoReference(SCargoTask* task)
    {
        using Release = void(__thiscall*)(SCargoTask*);
        reinterpret_cast<Release>(0x6916E0)(task);
    }

    void* __fastcall CargoDeletingDestructor(SCargoTask* task, void*, unsigned int flags)
    {
        const int kind = CargoKind(task);
        for (const auto& lease : cargoLeases)
        {
            if (task->entity == lease.second.native)
            {
                // Urgent replacement can delete the task between client pulses.
                // The retail destructor sets bRemoveFromWorld even for mission
                // objects, so protecting only DropEntity is insufficient.
                ReleaseCargoReference(task);
                break;
            }
        }
        using Destructor = void*(__thiscall*)(SCargoTask*, unsigned int);
        return reinterpret_cast<Destructor>(cargoDestructors[kind])(task, flags);
    }

    bool InstallCargoOwnershipGuard()
    {
        if (cargoDestructors[0])
            return true;
        // Refuse an incompatible executable/hook layout before modifying it.
        for (DWORD address : cargoTables)
        {
            auto* table = reinterpret_cast<TaskSimpleVTBL*>(address);
            if (table->ProcessPed != 0x693C40 || table->SetPedPosition != 0x6940A0 || table->MakeAbortable != 0x693BD0)
                return false;
        }
        for (int i = 0; i < 3; ++i)
        {
            cargoDestructors[i] = reinterpret_cast<TaskVTBL*>(cargoTables[i])->DeletingDestructor;
            MemPut<DWORD>(cargoTables[i], reinterpret_cast<DWORD>(&CargoDeletingDestructor));
        }
        return true;
    }

    class CCargoTaskSA final : public CTaskSimpleSA
    {
    public:
        explicit CCargoTaskSA(CObjectSAInterface* object)
        {
            CreateTaskInterface(object ? sizeof(SCargoTask) : sizeof(SCargoTask) + sizeof(float));
            if (!IsValid())
                return;
            if (object)
            {
                const CVector offset(0.0f, 0.0f, 0.0f);
                using Constructor = void(__thiscall*)(CTaskSAInterface*, CEntitySAInterface*, const CVector*, unsigned char, unsigned char, int, int, bool);
                // Secondary partial animation permits walking while carrying.
                reinterpret_cast<Constructor>(0x6913A0)(GetInterface(), object, &offset, 6, 1, static_cast<int>(eAnimID::ANIM_ID_CRRY_PRTIAL),
                                                        static_cast<int>(eAnimGroup::ANIM_GROUP_CARRY), false);
            }
            else
            {
                using Constructor = void(__thiscall*)(CTaskSAInterface*);
                reinterpret_cast<Constructor>(0x691990)(GetInterface());
            }
        }
    };

    CTaskManagerSA* CargoTaskManager(CPed* ped)
    {
        return ped && ped->GetPedIntelligence() ? dynamic_cast<CTaskManagerSA*>(ped->GetPedIntelligence()->GetTaskManager()) : nullptr;
    }

    template <typename Visitor>
    void VisitCargoTasks(CTaskManagerSA* manager, Visitor visitor)
    {
        auto visit = [&](CTaskSAInterface* task)
        {
            // Walk live trees, never a saved task pointer: GTA may replace or
            // clone either half of the secondary-hold/primary-putdown protocol.
            for (unsigned int depth = 0; task && depth < 32; ++depth)
            {
                if (CargoKind(task) >= 0)
                    visitor(reinterpret_cast<SCargoTask*>(task));
                using GetSubTask = CTaskSAInterface*(__thiscall*)(CTaskSAInterface*);
                task = reinterpret_cast<GetSubTask>(task->VTBL->GetSubTask)(task);
            }
        };
        for (auto* task : manager->GetInterface()->m_tasks)
            visit(task);
        for (auto* task : manager->GetInterface()->m_tasksSecondary)
            visit(task);
    }
}

bool CTasksSA::StartPedCarryObject(CPed* ped, CObject* object)
{
    auto* manager = CargoTaskManager(ped);
    auto* native = object ? object->GetObjectInterface() : nullptr;
    if (!manager || !native || cargoLeases.count(ped) || manager->GetInterface()->m_tasksSecondary[TASK_SECONDARY_PARTIAL_ANIM])
        return false;
    for (const auto& lease : cargoLeases)
        if (lease.second.native == native)
            return false;
    if (!InstallCargoOwnershipGuard())
        return false;

    SCargoLease lease{object,
                      native,
                      !!native->bUsesCollision,
                      !!native->bIsVisible,
                      !!native->bStreamingDontDelete,
                      !!native->bAttachedToEntity,
                      object->IsStatic(),
                      native->m_pMovingList != nullptr,
                      native->pad1};
    cargoLeases.emplace(ped, lease);
    // Offset 316 is the retail object type. Mission ownership prevents the
    // native abort/drop path from converting cargo into a hidden temp object.
    native->pad1 = 2;
    auto* task = NewTask<CCargoTaskSA>(native);
    if (!task)
    {
        CancelPedCarryObject(ped);
        return false;
    }
    m_pTaskManagementSystem->AddTask(task);
    manager->SetTaskSecondary(task, TASK_SECONDARY_PARTIAL_ANIM);
    return true;
}

bool CTasksSA::PutDownPedObject(CPed* ped)
{
    if (GetPedCarryState(ped) != 1 || !IsPedScriptCommandTaskReady(ped))
        return false;
    auto* task = NewTask<CCargoTaskSA>(static_cast<CObjectSAInterface*>(nullptr));
    if (!task)
        return false;
    m_pTaskManagementSystem->AddTask(task);
    if (AddPedScriptCommandTask(ped, task))
        return true;
    task->Destroy();
    return false;
}

int CTasksSA::GetPedCarryState(CPed* ped)
{
    auto  lease = cargoLeases.find(ped);
    auto* manager = CargoTaskManager(ped);
    if (lease == cargoLeases.end() || !manager)
        return 0;
    int state = 0;
    VisitCargoTasks(manager,
                    [&](SCargoTask* task)
                    {
                        if (task->entity == lease->second.native && !task->dropped)
                            state = std::max(state, CargoKind(task) == 2 ? 2 : (task->needsProcessing || !task->association ? 3 : 1));
                    });
    return state;
}

void CTasksSA::CancelPedCarryObject(CPed* ped)
{
    auto iter = cargoLeases.find(ped);
    if (iter == cargoLeases.end())
        return;
    const SCargoLease lease = iter->second;
    if (auto* manager = CargoTaskManager(ped))
    {
        VisitCargoTasks(manager,
                        [&](SCargoTask* task)
                        {
                            if (task->entity == lease.native)
                            {
                                ReleaseCargoReference(task);
                                task->dropped = true;
                            }
                        });
        // Only remove our secondary slot. Primary movement/event tasks remain
        // owned by GTA; a released putdown completes on its next ProcessPed.
        auto* secondary = manager->GetInterface()->m_tasksSecondary[TASK_SECONDARY_PARTIAL_ANIM];
        if (CargoKind(secondary) == 0 && reinterpret_cast<SCargoTask*>(secondary)->dropped)
            manager->RemoveTaskSecondary(TASK_SECONDARY_PARTIAL_ANIM);
    }
    lease.native->bUsesCollision = lease.collision;
    lease.native->bIsVisible = lease.visible;
    lease.native->bStreamingDontDelete = lease.streaming;
    lease.native->bAttachedToEntity = lease.attached;
    lease.native->pad1 = lease.objectType;
    lease.object->SetStatic(lease.stationary);
    using MovingList = void(__thiscall*)(CPhysicalSAInterface*);
    reinterpret_cast<MovingList>(lease.movingList ? FUNC_CPhysical_AddToMovingList : FUNC_CPhysical_RemoveFromMovingList)(lease.native);
    // Retail CPhysical::m_pEntityIgnoredCollision is at 0x128 (the legacy
    // interface calls it m_pControlCodeNodeLink). DropEntity writes the holder
    // without registering a reference; do not leave it pointing at a dead ped.
    static_assert(offsetof(CPhysicalSAInterface, m_pControlCodeNodeLink) == 0x128);
    if (reinterpret_cast<void*>(lease.native->m_pControlCodeNodeLink) == ped->GetPedInterface())
        lease.native->m_pControlCodeNodeLink = nullptr;
    cargoLeases.erase(iter);
}
