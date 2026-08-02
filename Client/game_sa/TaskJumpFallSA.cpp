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
#include "CEntitySA.h"
#include "TaskJumpFallSA.h"
#include <game/CAnimBlendAssocGroup.h>

namespace
{
    constexpr std::uintptr_t FUNC_CTaskSimpleClimb_StartAnim = 0x67DBE0;
    constexpr std::uintptr_t FUNC_CWorld_TestSphereAgainstWorld = 0x569E20;

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

    bool IsPhysicalClimbAnchor(const CEntitySAInterface* entity)
    {
        return entity && (entity->nType == ENTITY_TYPE_VEHICLE || entity->nType == ENTITY_TYPE_OBJECT);
    }

    bool IsExpectedClimbAnimation(eClimbHeights phase, unsigned short animationId)
    {
        switch (phase)
        {
            case CLIMB_GRAB:
                return animationId == static_cast<unsigned short>(eAnimID::ANIM_ID_CLIMB_JUMP) ||
                       animationId == static_cast<unsigned short>(eAnimID::ANIM_ID_CLIMB_IDLE);
            case CLIMB_PULLUP:
                return animationId == static_cast<unsigned short>(eAnimID::ANIM_ID_CLIMB_PULL);
            case CLIMB_STANDUP:
                return animationId == static_cast<unsigned short>(eAnimID::ANIM_ID_CLIMB_STAND);
            case CLIMB_FINISHED:
                return animationId == static_cast<unsigned short>(eAnimID::ANIM_ID_CLIMB_STAND_FINISH);
            case CLIMB_VAULT:
                return animationId == static_cast<unsigned short>(eAnimID::ANIM_ID_CLIMB_JUMP_B);
            case CLIMB_FINISHED_V:
                return animationId == static_cast<unsigned short>(eAnimID::ANIM_ID_CLIMB_JUMP2FALL);
            default:
                return false;
        }
    }

    CVector GetEntityPosition(const CEntitySAInterface* entity)
    {
        return entity->matrix ? entity->matrix->vPos : entity->m_transform.m_translate;
    }

    CVector TransformClimbHandholdToWorld(CEntitySAInterface* entity, const CVector& handhold)
    {
        if (!IsPhysicalClimbAnchor(entity))
            return handhold;

        if (entity->matrix)
        {
            CMatrix matrix;
            entity->matrix->ConvertToMatrix(matrix);
            return matrix.TransformVector(handhold);
        }

        const float cosine = std::cos(entity->m_transform.m_heading);
        const float sine = std::sin(entity->m_transform.m_heading);
        return CVector{entity->m_transform.m_translate.fX + handhold.fX * cosine - handhold.fY * sine,
                       entity->m_transform.m_translate.fY + handhold.fX * sine + handhold.fY * cosine, entity->m_transform.m_translate.fZ + handhold.fZ};
    }

    bool MatchesClimbAnchor(CEntitySAInterface* candidate, const SClimbTaskState& state, const CVector& candidateHandhold)
    {
        if (!candidate || candidate->m_nModelIndex != state.anchorModel || candidate->nType != state.anchorType)
            return false;

        const CVector worldHandhold = TransformClimbHandholdToWorld(candidate, candidateHandhold);
        return (worldHandhold - state.worldHandhold).LengthSquared() <= 2.25f && (GetEntityPosition(candidate) - state.anchorPosition).LengthSquared() <= 25.0f;
    }
}

CTaskComplexJumpSA::CTaskComplexJumpSA(bool bAllowClimb) : CTaskComplexJumpSA(bAllowClimb ? 0 : -1)
{
}

CTaskComplexJumpSA::CTaskComplexJumpSA(int iForceClimb)
{
    CreateTaskInterface(sizeof(CTaskComplexJumpSAInterface));
    if (!IsValid())
        return;

    // GTA uses -1 to disable climbing, 0 for ordinary player-controlled
    // scanning, and 1 to force the SimpleJump climb scan for an NPC. The
    // constructor still leaves player launch force disabled.
    iForceClimb = std::clamp(iForceClimb, -1, 1);
    DWORD dwFunc = FUNC_CTaskComplexJump__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

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

bool CTaskSimpleClimbSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                  float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleClimbSAInterface*>(GetInterface());
    return task && GetAirbornePresentationAnimation(task->m_pAnim, usAnimGroup, usAnimId, fProgress, fSpeed, fBlendAmount);
}

bool CTaskSimpleClimbSA::GetClimbTaskState(SClimbTaskState& state) const
{
    state = {};
    const auto* task = static_cast<const CTaskSimpleClimbSAInterface*>(GetInterface());
    if (!task || !task->m_pClimbEnt || !task->m_pAnim || task->m_nHeightForAnim < CLIMB_GRAB || task->m_nHeightForAnim > CLIMB_FINISHED_V ||
        task->m_nHeightForPos < CLIMB_NOT_READY || task->m_nHeightForPos > CLIMB_FINISHED_V || !std::isfinite(task->m_fHandholdHeading))
    {
        return false;
    }

    state.handhold = task->m_vecHandholdPos;
    state.worldHandhold = TransformClimbHandholdToWorld(task->m_pClimbEnt, task->m_vecHandholdPos);
    state.anchorPosition = GetEntityPosition(task->m_pClimbEnt);
    state.handholdHeading = task->m_fHandholdHeading;
    state.anchorModel = task->m_pClimbEnt->m_nModelIndex;
    state.anchorType = task->m_pClimbEnt->nType;
    state.surfaceType = task->m_nSurfaceType;
    state.animationPhase = task->m_nHeightForAnim;
    state.positionPhase = task->m_nHeightForPos;
    state.getToPositionCounter = task->m_nGetToPosCounter;
    state.forceClimb = task->m_bForceClimb;
    state.invalidClimb = task->m_bInvalidClimb;
    state.changePosition = task->m_bChangePosition;
    state.animationPlaying = task->m_pAnim->m_bPlaying;

    // A native phase edge can precede StartAnim by one ProcessPed frame. Do
    // not serialize the new phase with the previous association; the physical
    // hold lane keeps observers stable until both cursors describe one state.
    if (task->m_pAnim->sAnimGroup != static_cast<short>(eAnimGroup::ANIM_GROUP_DEFAULT) ||
        !IsExpectedClimbAnimation(state.animationPhase, static_cast<unsigned short>(task->m_pAnim->sAnimID)))
    {
        return false;
    }

    return std::isfinite(state.handhold.fX) && std::isfinite(state.handhold.fY) && std::isfinite(state.handhold.fZ) && std::isfinite(state.worldHandhold.fX) &&
           std::isfinite(state.worldHandhold.fY) && std::isfinite(state.worldHandhold.fZ) && std::isfinite(state.anchorPosition.fX) &&
           std::isfinite(state.anchorPosition.fY) && std::isfinite(state.anchorPosition.fZ);
}

bool CTaskSimpleClimbSA::PrepareTakeoverState(CPed* ped, const SClimbTaskState& state)
{
    auto* task = static_cast<CTaskSimpleClimbSAInterface*>(GetInterface());
    if (!task || !ped || state.animationPhase < CLIMB_GRAB || state.animationPhase > CLIMB_FINISHED_V || state.positionPhase < CLIMB_NOT_READY ||
        state.positionPhase > CLIMB_FINISHED_V || state.animationGroup != static_cast<unsigned short>(eAnimGroup::ANIM_GROUP_DEFAULT) ||
        !IsExpectedClimbAnimation(state.animationPhase, state.animationId) || !std::isfinite(state.animationProgress) || state.animationProgress < 0.0f ||
        state.animationProgress > 1.0f || !std::isfinite(state.animationSpeed) || state.animationSpeed <= 0.0f || state.animationSpeed > 16.0f ||
        !std::isfinite(state.animationBlendAmount) || state.animationBlendAmount < 0.0f || state.animationBlendAmount > 1.0f ||
        (state.animationPhase == CLIMB_PULLUP && state.positionPhase == CLIMB_NOT_READY))
    {
        return false;
    }

    using StartAnim = void(__thiscall*)(CTaskSimpleClimbSAInterface*, CPedSAInterface*);
    const auto startAnim = reinterpret_cast<StartAnim>(FUNC_CTaskSimpleClimb_StartAnim);

    const bool pausedStand = state.animationPhase == CLIMB_STANDUP && !state.animationPlaying;
    if (pausedStand)
    {
        // Low obstacles use this native special path before any association
        // exists. It creates a paused CLIMB_STAND whose next ProcessPed runs
        // TestForVault. Bootstrapping GRAB first would leave its DeleteAnimCB
        // attached and allow the fading association to orphan this live one.
        task->m_nHeightForAnim = CLIMB_PULLUP;
        task->m_nHeightForPos = CLIMB_NOT_READY;
        startAnim(task, ped->GetPedInterface());
    }
    else
    {
        // Establish a native-owned association and DeleteAnimCB before
        // selecting an ordinary transferred phase. PULLUP unconditionally
        // releases its previous association, so constructing directly there
        // would dereference null on the first ProcessPed call.
        startAnim(task, ped->GetPedInterface());
        if (!task->m_pAnim)
            return false;

        if (state.animationPhase == CLIMB_GRAB)
        {
            if (state.animationId == static_cast<unsigned short>(eAnimID::ANIM_ID_CLIMB_IDLE))
            {
                task->m_nHeightForAnim = CLIMB_GRAB;
                startAnim(task, ped->GetPedInterface());
            }
        }
        else
        {
            // A real vault transitions from CLIMB_STAND, which deliberately
            // uses a gradual blend delta of 16. Bootstrap that predecessor so
            // StartAnim retains GTA's stock STAND -> VAULT transition instead
            // of selecting its emergency immediate blend.
            if (state.animationPhase == CLIMB_VAULT)
            {
                task->m_nHeightForAnim = CLIMB_STANDUP;
                task->m_nHeightForPos = state.positionPhase > CLIMB_NOT_READY ? state.positionPhase : CLIMB_GRAB;
                startAnim(task, ped->GetPedInterface());
                if (!task->m_pAnim)
                    return false;
            }

            task->m_nHeightForAnim = state.animationPhase;
            task->m_nHeightForPos = state.positionPhase;
            startAnim(task, ped->GetPedInterface());
        }
    }

    if (!task->m_pAnim || task->m_pAnim->sAnimGroup != static_cast<short>(state.animationGroup) ||
        task->m_pAnim->sAnimID != static_cast<short>(state.animationId) || !task->m_pAnim->pAnimHierarchy ||
        !std::isfinite(task->m_pAnim->pAnimHierarchy->fTotalTime) || task->m_pAnim->pAnimHierarchy->fTotalTime <= 0.0f)
    {
        return false;
    }

    // StartAnim owns callback/flags and its phase-specific blend delta. Restore
    // only state that was already authoritative on the previous owner.
    task->m_nHeightForAnim = state.animationPhase;
    task->m_nHeightForPos = state.positionPhase;
    task->m_nGetToPosCounter = state.getToPositionCounter;
    task->m_bInvalidClimb = state.invalidClimb;
    task->m_bIsFinished = false;
    task->m_bChangeAnimation = false;
    task->m_bChangePosition = state.changePosition;

    task->m_pAnim->fCurrentTime = state.animationProgress * task->m_pAnim->pAnimHierarchy->fTotalTime;
    task->m_pAnim->fSpeed = state.animationSpeed;
    task->m_pAnim->fBlendAmount = state.animationBlendAmount;
    task->m_pAnim->m_bPlaying = state.animationPlaying;
    return true;
}

bool CTaskSimpleClimbSA::ApplyTakeoverAnimationProgress(float progress)
{
    auto* task = static_cast<CTaskSimpleClimbSAInterface*>(GetInterface());
    if (!task || !task->m_pAnim || !task->m_pAnim->pAnimHierarchy || !std::isfinite(progress) || progress < 0.0f || progress > 1.0f ||
        !std::isfinite(task->m_pAnim->pAnimHierarchy->fTotalTime) || task->m_pAnim->pAnimHierarchy->fTotalTime <= 0.0f)
    {
        return false;
    }

    task->m_pAnim->fCurrentTime = progress * task->m_pAnim->pAnimHierarchy->fTotalTime;
    return true;
}

CEntitySAInterface* CTaskSimpleClimbSA::ResolveTakeoverAnchor(CPed* ped, const SClimbTaskState& state)
{
    if (!ped || state.invalidClimb || state.animationPhase < CLIMB_GRAB || state.animationPhase > CLIMB_FINISHED_V)
        return nullptr;

    CVector climbPosition;
    float   climbHeading{};
    int     surfaceType{};
    if (CEntitySAInterface* scanned = CTaskSimpleClimb::TestForClimb(ped, climbPosition, climbHeading, surfaceType, true))
    {
        if (MatchesClimbAnchor(scanned, state, climbPosition))
            return scanned;
    }

    // During late pull-up or vault the ped can already be beyond the normal
    // launch scan volume. Resolve the serialized handhold against GTA's world
    // collision instead, then validate model, entity kind, and anchor origin
    // before giving the pointer to the native task constructor.
    auto* candidate = reinterpret_cast<CEntitySAInterface*(__cdecl*)(CVector, float, CEntitySAInterface*, bool, bool, bool, bool, bool, bool)>(
        FUNC_CWorld_TestSphereAgainstWorld)(state.worldHandhold, 0.75f, reinterpret_cast<CEntitySAInterface*>(ped->GetPedInterface()), true, true, false, true,
                                            true, false);
    return MatchesClimbAnchor(candidate, state, state.handhold) ? candidate : nullptr;
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
