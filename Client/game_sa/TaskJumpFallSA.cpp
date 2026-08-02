/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskJumpFallSA.cpp
 *  PURPOSE:     Jump and fall game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CAnimBlendAssociationSA.h"
#include "CAnimBlendHierarchySA.h"
#include "TaskJumpFallSA.h"

namespace
{
    bool GetAirbornePresentationAnimation(const CAnimBlendAssociationSAInterface* association, unsigned short& usAnimGroup, unsigned short& usAnimId,
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

CTaskComplexJumpSA::CTaskComplexJumpSA(bool bAllowClimb)
{
    CreateTaskInterface(sizeof(CTaskComplexJumpSAInterface));
    if (!IsValid())
        return;

    // CTaskComplexJump::eForceClimb uses -1 to disable climbing and 0 for
    // GTA's ordinary jump behaviour. The constructor leaves player launch
    // force disabled, matching a normal ambient ped jump.
    const int iForceClimb = bAllowClimb ? 0 : -1;
    DWORD     dwFunc = FUNC_CTaskComplexJump__Constructor;
    DWORD     dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    iForceClimb
        call    dwFunc
    }
    // clang-format on
}

CTaskComplexInAirAndLandSA::CTaskComplexInAirAndLandSA(bool bUsingJumpGlide, bool bUsingFallGlide)
{
    CreateTaskInterface(sizeof(CTaskComplexInAirAndLandSAInterface));
    if (!IsValid())
        return;

    DWORD dwFunc = FUNC_CTaskComplexInAirAndLand__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bUsingFallGlide
        push    bUsingJumpGlide
        call    dwFunc
    }
    // clang-format on
}

bool CTaskSimpleJumpSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                 float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleJumpSAInterface*>(GetInterface());
    return task && GetAirbornePresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

bool CTaskSimpleInAirSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                  float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleInAirSAInterface*>(GetInterface());
    return task && GetAirbornePresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

bool CTaskSimpleLandSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                 float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleLandSAInterface*>(GetInterface());
    return task && GetAirbornePresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

bool CTaskSimpleHitHeadSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                    float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleHitHeadSAInterface*>(GetInterface());
    return task && GetAirbornePresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

CTaskSimpleClimbSA::CTaskSimpleClimbSA(CEntitySAInterface* pClimbEnt, const CVector& vecTarget, float fHeading, unsigned char nSurfaceType,
                                       eClimbHeights nHeight, const bool bForceClimb)
{
    CreateTaskInterface(sizeof(CTaskSimpleClimbSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleClimb__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bForceClimb
        push    nHeight
        push    nSurfaceType
        push    fHeading
        push    vecTarget
        push    pClimbEnt
        call    dwFunc
    }
    // clang-format on
}

// ##############################################################################
// ## Name:    CTaskSimpleJetPack
// ## Purpose: Allows the player to use a jetpack to 'fly' around
// ##############################################################################

CTaskSimpleJetPackSA::CTaskSimpleJetPackSA(const CVector* pVecTargetPos, float fCruiseHeight, int nHoverTime)
{
    CreateTaskInterface(sizeof(CTaskSimpleJetPackSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleJetPack__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    0               // pTargetEnt - ignored for simplicity's sake (we really don't need it)
        push    nHoverTime
        push    fCruiseHeight
        push    pVecTargetPos
        call    dwFunc
    }
    // clang-format on
}
