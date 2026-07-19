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

CTaskComplexWanderStandard* CTasksSA::CreateTaskComplexWanderStandard(const int iMoveState, const char iDir, const bool bWanderSensibly)
{
    const char                    iNativeDir = iDir < 0 ? static_cast<char>(((int(__cdecl*)(int, int))0x407180)(0, 8)) : iDir;
    CTaskComplexWanderStandardSA* pTask = NewTask<CTaskComplexWanderStandardSA>(iMoveState, iNativeDir, bWanderSensibly);
    m_pTaskManagementSystem->AddTask(pTask);
    return pTask;
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
}
