/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskGoToSA.cpp
 *  PURPOSE:     Go-to game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "TaskGoToSA.h"

// ##############################################################################
// ## Name:    CTaskComplexWander
// ## Purpose: Generic task that makes peds wander around. Can't be used
// ##          directly, use a superclass of this instead.
// ##############################################################################

int CTaskComplexWanderSA::GetWanderType()
{
    CTaskSAInterface* pTaskInterface = GetInterface();
    DWORD             dwFunc = ((TaskComplexWanderVTBL*)pTaskInterface->VTBL)->GetWanderType;
    int               iReturn = NO_WANDER_TYPE;

    if (dwFunc && dwFunc != 0x82263A)  // some tasks have no wander type 0x82263A is purecal (assert?)
    {
        // clang-format off
        __asm
        {
            mov     ecx, pTaskInterface
            call    dwFunc
            mov     iReturn, eax
        }
        // clang-format on
    }
    return iReturn;
}

CNodeAddress* CTaskComplexWanderSA::GetNextNode()
{
    return &((CTaskComplexWanderSAInterface*)GetInterface())->m_NextNode;
}

CNodeAddress* CTaskComplexWanderSA::GetLastNode()
{
    return &((CTaskComplexWanderSAInterface*)GetInterface())->m_LastNode;
}

// ##############################################################################
// ## Name:    CTaskComplexWanderStandard
// ## Purpose: Standard class used for making normal peds wander around
// ##############################################################################

CTaskComplexWanderStandardSA::CTaskComplexWanderStandardSA(const int iMoveState, const unsigned char iDir, const bool bWanderSensibly)
{
    CreateTaskInterface(sizeof(CTaskComplexWanderStandardSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskComplexWanderStandard__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bWanderSensibly
        push    iDir
        push    iMoveState
        call    dwFunc
    }
    // clang-format on
}

CTaskComplexGoToPointAndStandStillSA::CTaskComplexGoToPointAndStandStillSA(const int iMoveState, const CVector& vecTarget, const float fTargetRadius,
                                                                           const float fSlowDownDistance)
{
    CreateTaskInterface(sizeof(CTaskComplexGoToPointAndStandStillSAInterface));
    if (!IsValid())
        return;

    DWORD dwFunc = FUNC_CTaskComplexGoToPointAndStandStill__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // The final two flags reproduce TASK_GO_STRAIGHT_TO_COORD: do not force
    // overshooting, and settle exactly at the requested point.
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    1
        push    0
        push    fSlowDownDistance
        push    fTargetRadius
        push    vecTarget
        push    iMoveState
        call    dwFunc
    }
    // clang-format on
}

CTaskComplexGoToPointAndStandStillTimedSA::CTaskComplexGoToPointAndStandStillTimedSA(const int iMoveState, const CVector& vecTarget, const float fTargetRadius,
                                                                                     const float fSlowDownDistance, const int iTime)
{
    CreateTaskInterface(sizeof(CTaskComplexGoToPointAndStandStillTimedSAInterface));
    if (!IsValid())
        return;

    DWORD dwFunc = FUNC_CTaskComplexGoToPointAndStandStillTimed__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    iTime
        push    fSlowDownDistance
        push    fTargetRadius
        push    vecTarget
        push    iMoveState
        call    dwFunc
    }
    // clang-format on
}

CTaskComplexSeekEntityRadiusAngleOffsetSA::CTaskComplexSeekEntityRadiusAngleOffsetSA(CPed* pTarget, int iTimeout, float fRadius, float fAngleDegrees)
{
    CreateTaskInterface(sizeof(CTaskComplexSeekEntityRadiusAngleOffsetSAInterface));
    if (!IsValid() || !pTarget)
        return;

    const int   iNativeTimeout = iTimeout < 0 ? 50000 : iTimeout;
    const float fMaxEntityDistance = 1.0f;
    const float fMoveStateRadius = 2.0f;
    const float fFollowNodeHeight = 2.0f;
    DWORD       dwFunc = FUNC_CTaskComplexSeekEntityRadiusAngleOffset__Constructor;
    DWORD       dwThisInterface = reinterpret_cast<DWORD>(GetInterface());
    DWORD       dwTargetInterface = reinterpret_cast<DWORD>(pTarget->GetPedInterface());

    // 06A8 delegates movement and path selection to GTA. Its relative radius
    // and angle are installed in the calculator after the generic seek ctor.
    // clang-format off
    __asm
    {
        push    1
        push    1
        push    fFollowNodeHeight
        push    fMoveStateRadius
        push    fMaxEntityDistance
        push    1000
        push    iNativeTimeout
        push    dwTargetInterface
        mov     ecx, dwThisInterface
        call    dwFunc
    }
    // clang-format on

    auto* pInterface = reinterpret_cast<CTaskComplexSeekEntityRadiusAngleOffsetSAInterface*>(GetInterface());
    pInterface->m_fRadius = fRadius;
    pInterface->m_fAngleRadians = fAngleDegrees * (3.14159265358979323846f / 180.0f);
}

CTaskComplexTurnToFaceEntityOrCoordSA::CTaskComplexTurnToFaceEntityOrCoordSA(CPed* pTarget)
{
    CreateTaskInterface(sizeof(CTaskComplexTurnToFaceEntityOrCoordSAInterface));
    if (!IsValid() || !pTarget)
        return;

    auto* pInterface = static_cast<CTaskComplexTurnToFaceEntityOrCoordSAInterface*>(GetInterface());
    auto* pTargetInterface = static_cast<CEntitySAInterface*>(pTarget->GetPedInterface());

    // Opcode 0639 uses GTA's entity constructor with these exact turn-rate and
    // angular-tolerance constants. The task keeps a safe reference to the live
    // target and builds its own AchieveHeading subtask.
    using Constructor = void(__thiscall*)(CTaskComplexTurnToFaceEntityOrCoordSAInterface*, CEntitySAInterface*, float, float);
    reinterpret_cast<Constructor>(FUNC_CTaskComplexTurnToFaceEntityOrCoord__Constructor)(pInterface, pTargetInterface, 0.5f, 0.2f);
}

namespace
{
    void DestroySequenceChild(CTaskSAInterface* pTask)
    {
        if (!pTask || !pTask->VTBL || !pTask->VTBL->DeletingDestructor)
            return;

        using DeletingDestructor = void(__thiscall*)(CTaskSAInterface*, unsigned char);
        reinterpret_cast<DeletingDestructor>(pTask->VTBL->DeletingDestructor)(pTask, 1);
    }
}  // namespace

CTaskComplexUseSequenceSA::CTaskComplexUseSequenceSA(CTaskSA* pTask, bool bRepeat)
{
    CTaskSAInterface* pTaskInterface = pTask && pTask->IsValid() ? pTask->GetInterface() : nullptr;
    if (pTaskInterface)
        pTask->SetInterface(nullptr);
    delete pTask;

    if (pTaskInterface)
    {
        CTaskSAInterface* tasks[] = {pTaskInterface};
        Initialize(tasks, 1, bRepeat);
    }
}

CTaskComplexUseSequenceSA::CTaskComplexUseSequenceSA(CTaskSAInterface* const* pTasks, size_t uiTaskCount, bool bRepeat)
{
    Initialize(pTasks, uiTaskCount, bRepeat);
}

void CTaskComplexUseSequenceSA::Initialize(CTaskSAInterface* const* pTasks, size_t uiTaskCount, bool bRepeat)
{
    if (!pTasks || uiTaskCount == 0 || uiTaskCount > 8)
        return;

    for (size_t i = 0; i < uiTaskCount; ++i)
    {
        if (!pTasks[i] || pTasks[i]->m_pParent)
        {
            for (size_t j = 0; j < uiTaskCount; ++j)
                DestroySequenceChild(pTasks[j]);
            return;
        }
    }

    int   iSequenceIndex = -1;
    DWORD dwFunc = FUNC_CTaskSequences__GetAvailableSlot;
    // clang-format off
    __asm
    {
        push    1
        call    dwFunc
        add     esp, 4
        mov     iSequenceIndex, eax
    }
    // clang-format on
    if (iSequenceIndex < 0 || iSequenceIndex >= 64)
    {
        for (size_t i = 0; i < uiTaskCount; ++i)
            DestroySequenceChild(pTasks[i]);
        return;
    }

    auto* pOpened = reinterpret_cast<bool*>(0xC17898);
    auto* pSequences = reinterpret_cast<CTaskComplexSequenceSAInterface*>(0xC178F0);
    auto* pSequence = &pSequences[iSequenceIndex];
    auto* pActiveSequence = reinterpret_cast<int*>(0x8D2E98);

    // Reproduce OPEN_SEQUENCE_TASK using a mission-cleanup slot. GTA keeps the
    // template globally while each CTaskComplexUseSequence clones its children.
    pOpened[iSequenceIndex] = true;
    dwFunc = FUNC_CTaskComplexSequence__Flush;
    DWORD dwSequenceInterface = reinterpret_cast<DWORD>(pSequence);
    // clang-format off
    __asm
    {
        mov     ecx, dwSequenceInterface
        call    dwFunc
    }
    // clang-format on
    *pActiveSequence = iSequenceIndex;

    dwFunc = FUNC_CTaskComplexSequence__AddTask;
    for (size_t i = 0; i < uiTaskCount; ++i)
    {
        CTaskSAInterface* pChildInterface = pTasks[i];
        // clang-format off
        __asm
        {
            push    pChildInterface
            mov     ecx, dwSequenceInterface
            call    dwFunc
        }
        // clang-format on
    }

    pSequence->m_uiRepeatMode = bRepeat ? 1u : 0u;
    pOpened[iSequenceIndex] = false;
    *pActiveSequence = -1;

    CreateTaskInterface(sizeof(CTaskComplexUseSequenceSAInterface));
    if (!IsValid())
    {
        dwFunc = FUNC_CTaskComplexSequence__Flush;
        // clang-format off
        __asm
        {
            mov     ecx, dwSequenceInterface
            call    dwFunc
        }
        // clang-format on
        return;
    }

    // PERFORM_SEQUENCE increments the global template reference count. CLEAR
    // then marks it for flushing when this native task releases its last clone.
    dwFunc = FUNC_CTaskComplexUseSequence__Constructor;
    DWORD dwThisInterface = reinterpret_cast<DWORD>(GetInterface());
    // clang-format off
    __asm
    {
        push    iSequenceIndex
        mov     ecx, dwThisInterface
        call    dwFunc
    }
    // clang-format on
    if (pSequence->m_uiReferenceCount == 0)
    {
        pSequence->m_bFlushTasks = false;
        dwFunc = FUNC_CTaskComplexSequence__Flush;
        // clang-format off
        __asm
        {
            mov     ecx, dwSequenceInterface
            call    dwFunc
        }
        // clang-format on
    }
    else
    {
        pSequence->m_bFlushTasks = true;
    }
}

int CTaskComplexUseSequenceSA::GetCurrentTaskIndex() const
{
    const auto* pInterface = static_cast<const CTaskComplexUseSequenceSAInterface*>(GetInterface());
    return pInterface ? pInterface->m_iCurrentTask : -1;
}
