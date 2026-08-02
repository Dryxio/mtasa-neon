/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskPhysicalResponseSA.cpp
 *  PURPOSE:     Physical response game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CAnimBlendAssociationSA.h"
#include "CAnimBlendHierarchySA.h"
#include "CGameSA.h"
#include "CPedSA.h"
#include "TaskPhysicalResponseSA.h"

extern CGameSA* pGame;

namespace
{
    bool GetTransientPresentationAnimation(const CAnimBlendAssociationSAInterface* association, unsigned short& usAnimGroup, unsigned short& usAnimId,
                                           float& fProgress, float& fSpeed, float& fBlendAmount)
    {
        if (!association || !association->pAnimHierarchy || !std::isfinite(association->pAnimHierarchy->fTotalTime) ||
            association->pAnimHierarchy->fTotalTime <= 0.0f || association->sAnimGroup < 0 || association->sAnimID < 0 ||
            !std::isfinite(association->fCurrentTime) || !std::isfinite(association->fSpeed) || association->fSpeed <= 0.0f ||
            !std::isfinite(association->fBlendAmount) || association->fBlendAmount <= 0.01f)
        {
            return false;
        }

        usAnimGroup = static_cast<unsigned short>(association->sAnimGroup);
        usAnimId = static_cast<unsigned short>(association->sAnimID);
        fProgress = std::clamp(association->fCurrentTime / association->pAnimHierarchy->fTotalTime, 0.0f, 1.0f);
        fSpeed = association->fSpeed;
        fBlendAmount = std::clamp(association->fBlendAmount, 0.0f, 1.0f);
        return true;
    }
}

bool CTaskSimpleEvasiveStepSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                        float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleEvasiveStepSAInterface*>(GetInterface());
    return task && GetTransientPresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

bool CTaskSimpleEvasiveDiveSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                        float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleEvasiveDiveSAInterface*>(GetInterface());
    return task && GetTransientPresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

bool CTaskSimpleShakeFistSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                      float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleShakeFistSAInterface*>(GetInterface());
    return task && GetTransientPresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

bool CTaskSimpleFallSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                 float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleFallSAInterface*>(GetInterface());
    return task && GetTransientPresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

bool CTaskSimpleGetUpSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                  float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleGetUpSAInterface*>(GetInterface());
    return task && GetTransientPresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

// ##############################################################################
// ## Name:    CTaskSimpleChoking
// ## Purpose: Make the ped choke
// ##############################################################################

CTaskSimpleChokingSA::CTaskSimpleChokingSA(CPed* pAttacker, bool bIsTearGas)
{
    CPedSA* pAttackerSA = dynamic_cast<CPedSA*>(pAttacker);

    DWORD dwFunc = FUNC_CTaskSimpleChoking__Constructor;
    DWORD dwIsTearGas = bIsTearGas;

    // Grab the GTA class for the attacker if any
    CPedSAInterface* pAttackerInterface = NULL;
    if (pAttackerSA)
        pAttackerInterface = pAttackerSA->GetPedInterface();

    CreateTaskInterface(sizeof(CTaskSimpleChokingSAInterface));
    if (!IsValid())
        return;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    ebx
        push    bIsTearGas
        push    pAttackerInterface
        call    dwFunc
        pop     ebx
    }
    // clang-format on
}

CPed* CTaskSimpleChokingSA::GetAttacker()
{
    CTaskSimpleChokingSAInterface* internalInterface = (CTaskSimpleChokingSAInterface*)GetInterface();
    SClientEntity<CPedSA>*         pPedClientEntity = pGame->GetPools()->GetPed((DWORD*)internalInterface->m_pAttacker);
    return pPedClientEntity ? pPedClientEntity->pEntity : nullptr;
}

unsigned int CTaskSimpleChokingSA::GetTimeRemaining()
{
    CTaskSimpleChokingSAInterface* internalInterface = (CTaskSimpleChokingSAInterface*)GetInterface();
    return internalInterface->m_nTimeRemaining;
}

unsigned int CTaskSimpleChokingSA::GetTimeStarted()
{
    CTaskSimpleChokingSAInterface* internalInterface = (CTaskSimpleChokingSAInterface*)GetInterface();
    return internalInterface->m_nTimeStarted;
}

bool CTaskSimpleChokingSA::IsTeargas()
{
    CTaskSimpleChokingSAInterface* internalInterface = (CTaskSimpleChokingSAInterface*)GetInterface();
    return internalInterface->m_bIsTeargas ? true : false;
}

bool CTaskSimpleChokingSA::IsFinished()
{
    CTaskSimpleChokingSAInterface* internalInterface = (CTaskSimpleChokingSAInterface*)GetInterface();
    return internalInterface->m_bIsFinished;
}

void CTaskSimpleChokingSA::UpdateChoke(CPed* pPed, CPed* pAttacker, bool bIsTearGas)
{
    // Get game interfaces
    CPedSA* pPedSA = dynamic_cast<CPedSA*>(pPed);
    if (!pPedSA)
        return;

    CPedSAInterface* pPedInterface = pPedSA->GetPedInterface();

    CPedSAInterface* pAttackerInterface = NULL;
    if (pAttacker)
    {
        CPedSA* pAttackerSA = dynamic_cast<CPedSA*>(pAttacker);
        if (pAttackerSA)
            pAttackerInterface = pAttackerSA->GetPedInterface();
    }

    // Call the func
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwFunc = FUNC_CTaskSimpleChoking__UpdateChoke;
    // clang-format off
    __asm
    {
        mov         ecx, dwThisInterface
        push        bIsTearGas
        push        pAttackerInterface
        push        pPedInterface
        call        dwFunc
    }
    // clang-format on
}
