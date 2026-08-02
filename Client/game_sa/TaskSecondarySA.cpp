/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskSecondarySA.cpp
 *  PURPOSE:     Secondary game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "TaskSecondarySA.h"
#include "CAnimBlendAssociationSA.h"
#include "CAnimBlendHierarchySA.h"

// ##############################################################################
// ## Name:    CTaskSimpleDuck
// ## Purpose: Make the ped duck
// ##############################################################################

CTaskSimpleDuckSA::CTaskSimpleDuckSA(eDuckControlTypes nDuckControl, unsigned short nLengthOfDuck, short nUseShotsWhizzingEvents)
{
    DWORD dwFunc = FUNC_CTaskSimpleDuck__Constructor;
    CreateTaskInterface(sizeof(CTaskSimpleDuckSAInterface));
    if (!IsValid())
        return;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    ebx
        mov     bx, nUseShotsWhizzingEvents
        push    ebx
        mov     bx, nLengthOfDuck
        push    ebx
        push    nDuckControl
        call    dwFunc
        pop     ebx
    }
    // clang-format on
}

bool CTaskSimpleDuckSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                 float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleDuckSAInterface*>(GetInterface());
    const auto* association = task ? reinterpret_cast<const CAnimBlendAssociationSAInterface*>(task->m_pDuckAnim) : nullptr;
    if (!association || !association->pAnimHierarchy || association->pAnimHierarchy->fTotalTime <= 0.0f || association->sAnimGroup < 0 ||
        association->sAnimID < 0 || !std::isfinite(association->fCurrentTime) || !std::isfinite(association->fSpeed) || association->fSpeed <= 0.0f ||
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
