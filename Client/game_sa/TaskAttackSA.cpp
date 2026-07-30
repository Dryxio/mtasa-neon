/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskAttackSA.cpp
 *  PURPOSE:     Attack game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "TaskAttackSA.h"
#include "CAnimBlendAssociationSA.h"
#include "CAnimBlendHierarchySA.h"
#include "CEntitySA.h"
#include "CGameSA.h"
#include "CPedSA.h"

extern CGameSA* pGame;

namespace
{
    constexpr unsigned char MAX_DRIVEBY_STYLE = 8;

    bool ResolvePresentationTarget(CEntity* targetEntity, const CVector& coordinateTarget, CVector& resolvedTarget)
    {
        if (!targetEntity)
        {
            resolvedTarget = coordinateTarget;
            return std::isfinite(resolvedTarget.fX) && std::isfinite(resolvedTarget.fY) && std::isfinite(resolvedTarget.fZ);
        }

        const auto* target = reinterpret_cast<const CEntitySAInterface*>(targetEntity);
        if (target->nType == ENTITY_TYPE_PED)
        {
            SClientEntity<CPedSA>* pedEntity = pGame->GetPools()->GetPed(reinterpret_cast<DWORD*>(targetEntity));
            if (pedEntity && pedEntity->pEntity && pedEntity->pEntity->GetBonePosition(BONE_SPINE1, &resolvedTarget))
                return true;
        }

        resolvedTarget = target->matrix ? target->matrix->vPos : target->m_transform.m_translate;
        return std::isfinite(resolvedTarget.fX) && std::isfinite(resolvedTarget.fY) && std::isfinite(resolvedTarget.fZ);
    }
}

CTaskComplexKillPedOnFootSA::CTaskComplexKillPedOnFootSA(CPed* pTarget)
{
    CreateTaskInterface(sizeof(CTaskComplexKillPedOnFootSAInterface));
    if (!IsValid() || !pTarget)
        return;

    DWORD dwFunc = FUNC_CTaskComplexKillPedOnFoot__Constructor;
    DWORD dwThisInterface = reinterpret_cast<DWORD>(GetInterface());
    DWORD dwTargetInterface = reinterpret_cast<DWORD>(pTarget->GetPedInterface());
    // 05E2 deliberately leaves GTA in control of weapon choice, pursuit,
    // aiming, melee fallback and vehicle handling for this indefinite task.
    // clang-format off
    __asm
    {
        push    1
        push    0
        push    0
        push    0
        push    -1
        push    dwTargetInterface
        mov     ecx, dwThisInterface
        call    dwFunc
    }
    // clang-format on
}

CTaskSimpleGangDriveBySA::CTaskSimpleGangDriveBySA(CEntity* pTargetEntity, const CVector* pVecTarget, float fAbortRange, char FrequencyPercentage,
                                                   char nDrivebyStyle, bool bSeatRHS)
{
    CreateTaskInterface(sizeof(CTaskSimpleGangDriveBySAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleGangDriveBy__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwTargetEntity = (pTargetEntity) ? (DWORD)pTargetEntity->GetInterface() : 0;
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bSeatRHS
        push    nDrivebyStyle
        push    FrequencyPercentage
        push    fAbortRange
        push    pVecTarget
        push    dwTargetEntity
        call    dwFunc
    }
    // clang-format on
}

void CTaskSimpleGangDriveBySA::SetFromScriptCommand(bool bFromScriptCommand)
{
    if (!IsValid())
        return;

    // Opcode 0713 writes this byte after construction, before routing the task
    // through GTA's scripted-event path. Preserve that native distinction.
    static_cast<CTaskSimpleGangDriveBySAInterface*>(GetInterface())->m_bFromScriptCommand = bFromScriptCommand;
}

bool CTaskSimpleGangDriveBySA::GetPresentation(CVector& vecTarget, float& fAbortRange, unsigned char& ucFrequencyPercentage, unsigned char& ucDriveByStyle,
                                               bool& bSeatRHS)
{
    if (!IsValid())
        return false;

    const auto* task = static_cast<const CTaskSimpleGangDriveBySAInterface*>(GetInterface());
    if (!ResolvePresentationTarget(task->m_pTargetEntity, task->m_vecCoords, vecTarget))
        return false;

    fAbortRange = task->m_fAbortRange;
    ucFrequencyPercentage = static_cast<unsigned char>(task->m_nFrequencyPercentage);
    ucDriveByStyle = static_cast<unsigned char>(task->m_nDrivebyStyle);
    bSeatRHS = task->m_bSeatRHS;
    // GTA only defines drive-by styles 0..8. Do not export corrupt native task
    // state into a presentation packet that another client would construct.
    return std::isfinite(vecTarget.fX) && std::isfinite(vecTarget.fY) && std::isfinite(vecTarget.fZ) && std::isfinite(fAbortRange) && fAbortRange >= 0.0f &&
           ucFrequencyPercentage <= 100 && ucDriveByStyle <= MAX_DRIVEBY_STYLE;
}

void CTaskSimpleGangDriveBySA::SetPresentationTarget(const CVector& vecTarget)
{
    if (!IsValid())
        return;

    auto* task = static_cast<CTaskSimpleGangDriveBySAInterface*>(GetInterface());
    // Presentation clones deliberately use a coordinate rather than retaining
    // another client's native entity pointer. Updating that coordinate in
    // place keeps a moving target smooth without restarting the drive-by task.
    task->m_pTargetEntity = nullptr;
    task->m_vecCoords = vecTarget;
}

CTaskSimpleUseGunSA::CTaskSimpleUseGunSA(CEntity* pTargetEntity, CVector vecTarget, char nCommand, short nBurstLength, unsigned char bAimImmediate)
{
    CreateTaskInterface(sizeof(CTaskSimpleUseGunSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwTargetEntity = (pTargetEntity) ? (DWORD)pTargetEntity->GetInterface() : 0;
    float fTargetX = vecTarget.fX, fTargetY = vecTarget.fY, fTargetZ = vecTarget.fZ;
    DWORD dwBurstLength = nBurstLength;
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bAimImmediate
        push    dwBurstLength
        push    nCommand
        push    fTargetZ
        push    fTargetY
        push    fTargetX
        push    dwTargetEntity
        call    dwFunc
    }
    // clang-format on
}

bool CTaskSimpleUseGunSA::SetPedPosition(CPed* pPed)
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_SetPedPosition;
    DWORD dwThisInterface = (DWORD)GetInterface();

    BYTE* ptr = (BYTE*)dwThisInterface;
    ptr[0x0d] = 2;

    CPedSAInterface* dwPedInterface = (CPedSAInterface*)pPed->GetInterface();

    BYTE currentWeaponSlot = dwPedInterface->bCurrentWeaponSlot;

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    dwPedInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

void CTaskSimpleUseGunSA::FireGun(CPed* pPed, bool bFlag)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_FireGun;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bFlag
        push    dwPedInterface
        call    dwFunc
    }
    // clang-format on
}

bool CTaskSimpleUseGunSA::ControlGun(CPed* pPed, CEntity* pTargetEntity, char nCommand)
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_ControlGun;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    DWORD dwTargetEntity = (pTargetEntity) ? (DWORD)pTargetEntity->GetInterface() : 0;
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    nCommand
        push    dwTargetEntity
        push    dwPedInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

bool CTaskSimpleUseGunSA::ControlGunMove(CVector2D* pMoveVec)
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_ControlGunMove;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    pMoveVec
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

void CTaskSimpleUseGunSA::Reset(CPed* pPed, CEntity* pTargetEntity, CVector vecTarget, char nCommand, short nBurstLength)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_Reset;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    DWORD dwTargetEntity = (pTargetEntity) ? (DWORD)pTargetEntity->GetInterface() : 0;
    float fTargetX = vecTarget.fX, fTargetY = vecTarget.fY, fTargetZ = vecTarget.fZ;
    DWORD dwBurstLength = nBurstLength;
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    dwBurstLength
        push    nCommand
        push    fTargetZ
        push    fTargetY
        push    fTargetX
        push    dwTargetEntity
        push    dwPedInterface
        call    dwFunc
    }
    // clang-format on
}

int CTaskSimpleUseGunSA::GetTaskType()
{
    int   iReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_GetTaskType;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
        mov     iReturn, eax
    }
    // clang-format on
    return iReturn;
}

// If flag is 1 or 2 then do magic stop stuff else set m_nNextCommand to 6
// (pEvent not used)
bool CTaskSimpleUseGunSA::MakeAbortable(CPed* pPed, int iPriority, CEvent const* pEvent)
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_MakeAbortable;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    pEvent
        push    iPriority
        push    dwPedInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

bool CTaskSimpleUseGunSA::ProcessPed(CPed* pPed)
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_ProcessPed;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    dwPedInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

void CTaskSimpleUseGunSA::AbortIK(CPed* pPed)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_AbortIK;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    dwPedInterface
        call    dwFunc
    }
    // clang-format on
}

void CTaskSimpleUseGunSA::AimGun(CPed* pPed)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_AimGun;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    dwPedInterface
        call    dwFunc
    }
    // clang-format on
}

void CTaskSimpleUseGunSA::ClearAnim(CPed* pPed)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_ClearAnim;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    dwPedInterface
        call    dwFunc
    }
    // clang-format on
}

signed char CTaskSimpleUseGunSA::GetCurrentCommand()
{
    signed char bReturn;
    DWORD       dwFunc = FUNC_CTaskSimpleUseGun_GetCurrentCommand;
    DWORD       dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

bool CTaskSimpleUseGunSA::GetDoneFiring()
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_GetDoneFiring;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

bool CTaskSimpleUseGunSA::GetIsFinished()
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_GetIsFinished;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

bool CTaskSimpleUseGunSA::IsLineOfSightBlocked()
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_IsLineOfSightBlocked;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

bool CTaskSimpleUseGunSA::GetIsFiring()
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_GetIsFiring;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

short CTaskSimpleUseGunSA::GetBurstLength()
{
    if (!IsValid())
        return 1;

    return static_cast<const CTaskSimpleUseGunSAInterface*>(GetInterface())->m_nBurstLength;
}

bool CTaskSimpleUseGunSA::GetPresentationTarget(CVector& vecTarget)
{
    if (!IsValid())
        return false;

    const auto* task = static_cast<const CTaskSimpleUseGunSAInterface*>(GetInterface());
    return ResolvePresentationTarget(task->m_pTargetEntity, task->m_vecCoords, vecTarget);
}

bool CTaskSimpleUseGunSA::IsPresentationFiringLeftHand() const
{
    if (!GetInterface())
        return false;

    const unsigned char fireThisFrame = static_cast<const CTaskSimpleUseGunSAInterface*>(GetInterface())->m_nFireGunThisFrame;
    // Dual-wield UseGun fires the right hand first and clears bit zero before
    // entering CWeapon::Fire for the left hand.
    return !(fireThisFrame & 0x1) && (fireThisFrame & 0x2);
}

void CTaskSimpleUseGunSA::SetPresentationTarget(const CVector& vecTarget)
{
    if (!IsValid())
        return;

    auto* task = static_cast<CTaskSimpleUseGunSAInterface*>(GetInterface());
    // Only coordinate-backed presentation clones may use this shortcut. A
    // native entity target owns a GTA reference that must not be severed by a
    // viewer-side smoothing update.
    if (!task->m_pTargetEntity)
        task->m_vecCoords = vecTarget;
}

bool CTaskSimpleUseGunSA::GetIsReloading()
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_GetIsReloading;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

bool CTaskSimpleUseGunSA::GetSkipAim()
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_GetSkipAim;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

bool CTaskSimpleUseGunSA::PlayerPassiveControlGun()
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_PlayerPassiveControlGun;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

void CTaskSimpleUseGunSA::RemoveStanceAnims(CPed* pPed, float f)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_RemoveStanceAnims;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    f
        push    dwPedInterface
        call    dwFunc
    }
    // clang-format on
}

bool CTaskSimpleUseGunSA::RequirePistolWhip(CPed* pPed, CEntity* pTargetEntity)
{
    bool  bReturn;
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_ControlGun;
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    DWORD dwTargetEntity = (pTargetEntity) ? (DWORD)pTargetEntity->GetInterface() : 0;
    // clang-format off
    __asm
    {
        push    dwTargetEntity
        push    dwPedInterface
        call    dwFunc
        mov     bReturn, al
    }
    // clang-format on
    return bReturn;
}

void CTaskSimpleUseGunSA::SetBurstLength(short a)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_SetBurstLength;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    a
        call    dwFunc
    }
    // clang-format on
}

void CTaskSimpleUseGunSA::SetMoveAnim(CPed* pPed)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_SetMoveAnim;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    dwPedInterface
        call    dwFunc
    }
    // clang-format on
}

void CTaskSimpleUseGunSA::StartAnim(class CPed* pPed)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_StartAnim;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    dwPedInterface
        call    dwFunc
    }
    // clang-format on
}

void CTaskSimpleUseGunSA::StartCountDown(unsigned char a, bool b)
{
    DWORD dwFunc = FUNC_CTaskSimpleUseGun_StartCountDown;
    DWORD dwThisInterface = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    b
        push    a
        call    dwFunc
    }
    // clang-format on
}

CTaskSimpleGunControlSA::CTaskSimpleGunControlSA(CEntity* pTargetEntity, const CVector* pVecTarget, const CVector* pVecMoveTarget, char nFiringTask,
                                                 short nBurstLength, int iDuration)
{
    CreateTaskInterface(sizeof(CTaskSimpleGunControlSAInterface));
    if (!IsValid())
        return;

    DWORD dwFunc = FUNC_CTaskSimpleGunControl__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwTargetEntity = pTargetEntity ? (DWORD)pTargetEntity->GetInterface() : 0;
    DWORD dwFiringTask = static_cast<DWORD>(static_cast<unsigned char>(nFiringTask));
    DWORD dwBurstLength = static_cast<DWORD>(static_cast<unsigned short>(nBurstLength));
    // Call GTA's verified constructor rather than reproducing its task state
    // machine, so aiming, weapon use and cleanup retain native semantics.
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    iDuration
        push    dwBurstLength
        push    dwFiringTask
        push    pVecMoveTarget
        push    pVecTarget
        push    dwTargetEntity
        call    dwFunc
    }
    // clang-format on
}

void CTaskSimpleGunControlSA::SetPresentationTarget(const CVector& vecTarget)
{
    if (!IsValid())
        return;

    auto* task = static_cast<CTaskSimpleGunControlSAInterface*>(GetInterface());
    // Presentation tasks are constructed with a coordinate and no native
    // target reference. Keep that contract explicit so this helper can never
    // mutate ownership of an authoritative GTA target.
    if (!task->m_pTargetEntity)
        task->m_vecTarget = vecTarget;
}

CTaskSimpleFightSA::CTaskSimpleFightSA(CEntity* pTargetEntity, int nCommand, unsigned int nIdlePeriod)
{
    CreateTaskInterface(sizeof(CTaskSimpleFightSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleFight__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwTargetEntity = (pTargetEntity) ? (DWORD)pTargetEntity->GetInterface() : 0;
    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    nIdlePeriod
        push    nCommand
        push    dwTargetEntity
        call    dwFunc
    }
    // clang-format on
}

bool CTaskSimpleFightSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                  float& fBlendAmount) const
{
    const auto* task = static_cast<const CTaskSimpleFightSAInterface*>(GetInterface());
    const auto* strike = reinterpret_cast<const CAnimBlendAssociationSAInterface*>(task->m_pAnim);
    const auto* idle = reinterpret_cast<const CAnimBlendAssociationSAInterface*>(task->m_pIdleAnim);
    const auto* association = strike && (!idle || strike->fBlendAmount >= idle->fBlendAmount) ? strike : idle;
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
