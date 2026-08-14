/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskBasicSA.cpp
 *  PURPOSE:     Basic game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "TaskBasicSA.h"
#include "CAnimBlendAssociationSA.h"
#include "CAnimBlendHierarchySA.h"
#include "CPedSA.h"

namespace
{
    void GetLiveSimpleAnimationDiagnostic(const CTaskSimpleAnimSAInterface* task, SNamedAnimPresentationDiagnostic& diagnostic)
    {
        diagnostic = {};
        // SimpleAnim is only safe to present while GTA still owns a live,
        // blended association. The shared prefix is verified for RunAnim and
        // PlayHandSignalAnim; no derived fields or task state are inferred.
        const auto* association = task && task->m_pAnim ? reinterpret_cast<const CAnimBlendAssociationSAInterface*>(task->m_pAnim) : nullptr;
        diagnostic.association = association;
        if (!association)
        {
            diagnostic.validation = ENamedAnimPresentationValidation::NO_ASSOCIATION;
            return;
        }

        diagnostic.hierarchy = association->pAnimHierarchy;
        diagnostic.animGroup = association->sAnimGroup;
        diagnostic.animId = association->sAnimID;
        diagnostic.currentTime = association->fCurrentTime;
        diagnostic.speed = association->fSpeed;
        diagnostic.blendAmount = association->fBlendAmount;
        if (!association->pAnimHierarchy)
        {
            diagnostic.validation = ENamedAnimPresentationValidation::NO_HIERARCHY;
            return;
        }

        diagnostic.totalTime = association->pAnimHierarchy->fTotalTime;
        if (diagnostic.totalTime <= 0.0f)
            diagnostic.validation = ENamedAnimPresentationValidation::INVALID_TOTAL_TIME;
        else if (diagnostic.animGroup < 0)
            diagnostic.validation = ENamedAnimPresentationValidation::INVALID_ANIM_GROUP;
        else if (diagnostic.animId < 0)
            diagnostic.validation = ENamedAnimPresentationValidation::INVALID_ANIM_ID;
        else if (!std::isfinite(diagnostic.currentTime))
            diagnostic.validation = ENamedAnimPresentationValidation::INVALID_CURRENT_TIME;
        else if (!std::isfinite(diagnostic.speed))
            diagnostic.validation = ENamedAnimPresentationValidation::INVALID_SPEED;
        else if (diagnostic.speed <= 0.0f)
            diagnostic.validation = ENamedAnimPresentationValidation::NON_POSITIVE_SPEED;
        else if (!std::isfinite(diagnostic.blendAmount))
            diagnostic.validation = ENamedAnimPresentationValidation::INVALID_BLEND_AMOUNT;
        else if (diagnostic.blendAmount <= 0.01f)
            diagnostic.validation = ENamedAnimPresentationValidation::INACTIVE_BLEND_AMOUNT;
        else
            diagnostic.validation = ENamedAnimPresentationValidation::VALID;
    }

    bool GetLiveSimpleAnimationPresentation(const CTaskSimpleAnimSAInterface* task, unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress,
                                            float& fSpeed, float& fBlendAmount)
    {
        SNamedAnimPresentationDiagnostic diagnostic;
        GetLiveSimpleAnimationDiagnostic(task, diagnostic);
        if (diagnostic.validation != ENamedAnimPresentationValidation::VALID)
            return false;

        usAnimGroup = static_cast<unsigned short>(diagnostic.animGroup);
        usAnimId = static_cast<unsigned short>(diagnostic.animId);
        fProgress = std::clamp(diagnostic.currentTime / diagnostic.totalTime, 0.0f, 1.0f);
        fSpeed = diagnostic.speed;
        fBlendAmount = std::clamp(diagnostic.blendAmount, 0.0f, 1.0f);
        return true;
    }
}

CTaskComplexPartnerChatSA::CTaskComplexPartnerChatSA(CPed* pPartner, bool bLeadSpeaker, bool bUpdateDirection, bool bConversationEnabled)
{
    CreateTaskInterface(sizeof(CTaskComplexPartnerChatSAInterface));
    if (!IsValid() || !pPartner)
        return;

    DWORD       dwFunc = FUNC_CTaskComplexPartnerChat__Constructor;
    DWORD       dwThisInterface = reinterpret_cast<DWORD>(GetInterface());
    DWORD       dwPartnerInterface = reinterpret_cast<DWORD>(pPartner->GetPedInterface());
    const char* pCommandName = "COMMAND_TASK_CHAT_WITH_CHAR";
    const int   iUpdateDirectionCount = bUpdateDirection ? -1 : 4;

    // Construct through the exact 0677 path first. GTA forces the direction
    // counter to 4 while conversation audio is enabled at 0x6842EB. When
    // RequestPedConversation later fails, 0x681F6C clears only the conversation
    // flag. Reproducing that post-failure state is essential for the timed
    // native fallback; constructing with the flag already false leaves the
    // counter at the opcode's -1 sentinel and makes the pair terminate early.
    // clang-format off
    __asm
    {
        push    0
        push    0
        push    0
        push    1
        push    1
        push    iUpdateDirectionCount
        push    03f000000h
        push    bLeadSpeaker
        push    dwPartnerInterface
        push    pCommandName
        mov     ecx, dwThisInterface
        call    dwFunc
    }
    // clang-format on

    if (!bConversationEnabled) reinterpret_cast<CTaskComplexPartnerChatSAInterface*>(GetInterface())->SetConversationEnabled(false);
}

CTaskSimpleStandStillSA::CTaskSimpleStandStillSA(int iDuration)
{
    CreateTaskInterface(sizeof(CTaskSimpleStandStillSAInterface));
    if (!IsValid())
        return;

    DWORD dwFunc = FUNC_CTaskSimpleStandStill__Constructor;
    DWORD dwThisInterface = reinterpret_cast<DWORD>(GetInterface());
    // clang-format off
    __asm
    {
        push    041000000h
        push    0
        push    0
        push    iDuration
        mov     ecx, dwThisInterface
        call    dwFunc
    }
    // clang-format on
}

void CTaskSimpleAnimSA::GetPresentationDiagnostic(SNamedAnimPresentationDiagnostic& diagnostic) const noexcept
{
    GetLiveSimpleAnimationDiagnostic(static_cast<const CTaskSimpleAnimSAInterface*>(GetInterface()), diagnostic);
}

bool CTaskSimpleAnimSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                 float& fBlendAmount) const
{
    return GetLiveSimpleAnimationPresentation(static_cast<const CTaskSimpleAnimSAInterface*>(GetInterface()), usAnimGroup, usAnimId, fProgress, fSpeed,
                                              fBlendAmount);
}

CTaskSimpleRunNamedAnimSAInterface* CTaskSimpleRunNamedAnimSA::GetAnimationInterface() noexcept
{
    return reinterpret_cast<CTaskSimpleRunNamedAnimSAInterface*>(this->GetInterface());
}

const CTaskSimpleRunNamedAnimSAInterface* CTaskSimpleRunNamedAnimSA::GetAnimationInterface() const noexcept
{
    return reinterpret_cast<const CTaskSimpleRunNamedAnimSAInterface*>(this->GetInterface());
}

const char* CTaskSimpleRunNamedAnimSA::GetAnimName() const noexcept
{
    return GetAnimationInterface()->m_animName;
}

const char* CTaskSimpleRunNamedAnimSA::GetGroupName() const noexcept
{
    return GetAnimationInterface()->m_animGroupName;
}

void CTaskSimpleRunNamedAnimSA::GetPresentationDiagnostic(SNamedAnimPresentationDiagnostic& diagnostic) const noexcept
{
    GetLiveSimpleAnimationDiagnostic(static_cast<const CTaskSimpleAnimSAInterface*>(GetInterface()), diagnostic);
}

bool CTaskSimpleRunNamedAnimSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                         float& fBlendAmount) const
{
    SNamedAnimPresentationDiagnostic diagnostic;
    GetPresentationDiagnostic(diagnostic);
    if (diagnostic.validation != ENamedAnimPresentationValidation::VALID)
        return false;

    usAnimGroup = static_cast<unsigned short>(diagnostic.animGroup);
    usAnimId = static_cast<unsigned short>(diagnostic.animId);
    fProgress = std::clamp(diagnostic.currentTime / diagnostic.totalTime, 0.0f, 1.0f);
    fSpeed = diagnostic.speed;
    fBlendAmount = std::clamp(diagnostic.blendAmount, 0.0f, 1.0f);
    return true;
}

CTaskComplexUseMobilePhoneSA::CTaskComplexUseMobilePhoneSA(const int iDuration)
{
    CreateTaskInterface(sizeof(CTaskComplexUseMobilePhoneSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskComplexUseMobilePhone__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    iDuration
        call    dwFunc
    }
    // clang-format on
}

CTaskSimpleRunAnimSA::CTaskSimpleRunAnimSA(const AssocGroupId animGroup, const AnimationId animID, const float fBlendDelta, const int iTaskType,
                                           const char* pTaskName, const bool bHoldLastFrame)
{
    // TODO: Find out the real size
    CreateTaskInterface(1024);
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleRunAnim__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bHoldLastFrame
        push    pTaskName
        push    iTaskType
        push    fBlendDelta
        push    animID
        push    animGroup
        call    dwFunc
    }
    // clang-format on
}

bool CTaskSimpleRunAnimSA::GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed,
                                                    float& fBlendAmount) const
{
    return GetLiveSimpleAnimationPresentation(static_cast<const CTaskSimpleAnimSAInterface*>(GetInterface()), usAnimGroup, usAnimId, fProgress, fSpeed,
                                              fBlendAmount);
}

void CTaskSimpleRunAnimSA::GetPresentationDiagnostic(SNamedAnimPresentationDiagnostic& diagnostic) const noexcept
{
    GetLiveSimpleAnimationDiagnostic(static_cast<const CTaskSimpleAnimSAInterface*>(GetInterface()), diagnostic);
}

CTaskSimpleRunNamedAnimSA::CTaskSimpleRunNamedAnimSA(const char* pAnimName, const char* pAnimGroupName, const int flags, const float fBlendDelta,
                                                     const int iTime, const bool bDontInterrupt, const bool bRunInSequence, const bool bOffsetPed,
                                                     const bool bHoldLastFrame)
{
    // TODO: Find out the real size
    CreateTaskInterface(sizeof(CTaskSimpleRunNamedAnimSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleRunNamedAnim__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bHoldLastFrame
        push    bOffsetPed
        push    bRunInSequence
        push    bDontInterrupt
        push    iTime
        push    fBlendDelta
        push    flags
        push    pAnimGroupName
        push    pAnimName
        call    dwFunc
    }
    // clang-format on
}

CTaskComplexDieSA::CTaskComplexDieSA(const eWeaponType eMeansOfDeath, const AssocGroupId animGroup, const AnimationId anim, const float fBlendDelta,
                                     const float fAnimSpeed, const bool bBeingKilledByStealth, const bool bFallingToDeath, const int iFallToDeathDir,
                                     const bool bFallToDeathOverRailing)
{
    CreateTaskInterface(sizeof(CTaskComplexDieSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskComplexDie__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bFallToDeathOverRailing
        push    iFallToDeathDir
        push    bFallingToDeath
        push    bBeingKilledByStealth
        push    fAnimSpeed
        push    fBlendDelta
        push    anim
        push    animGroup
        push    eMeansOfDeath
        call    dwFunc
    }
    // clang-format on
}

CTaskComplexSmartFleeEntitySA::CTaskComplexSmartFleeEntitySA(CPed* pTarget, bool bScream, float fSafeDistance, int iDuration, int iPositionCheckPeriod,
                                                             float fPositionChangeTolerance)
{
    CreateTaskInterface(sizeof(CTaskComplexSmartFleeEntitySAInterface));
    if (!IsValid() || !pTarget)
        return;

    auto* pInterface = static_cast<CTaskComplexSmartFleeEntitySAInterface*>(GetInterface());
    auto* pTargetInterface = pTarget->GetPedInterface();
    using Constructor = void(__thiscall*)(CTaskComplexSmartFleeEntitySAInterface*, CEntitySAInterface*, bool, float, int, int, float);
    reinterpret_cast<Constructor>(FUNC_CTaskComplexSmartFleeEntity__Constructor)(pInterface, pTargetInterface, bScream, fSafeDistance, iDuration,
                                                                                 iPositionCheckPeriod, fPositionChangeTolerance);
}

CTaskSimpleStealthKillSA::CTaskSimpleStealthKillSA(bool bKiller, CPed* pPed, const AssocGroupId animGroup)
{
    // TODO: Find out the real size
    CreateTaskInterface(1024);
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleStealthKill__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPed->GetPedInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    animGroup
        push    dwPedInterface
        push    bKiller
        call    dwFunc
    }
    // clang-format on
}

CTaskSimpleDeadSA::CTaskSimpleDeadSA(unsigned int uiDeathTimeMS, bool bUnk2)
{
    CreateTaskInterface(sizeof(CTaskSimpleDeadSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleDead__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bUnk2
        push    uiDeathTimeMS
        call    dwFunc
    }
    // clang-format on
}

CTaskSimpleBeHitSA::CTaskSimpleBeHitSA(CPed* pPedAttacker, ePedPieceTypes hitBodyPart, int hitBodySide, int weaponId)
{
    CreateTaskInterface(sizeof(CTaskSimpleBeHitSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskSimpleBeHit__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwPedInterface = (DWORD)pPedAttacker->GetPedInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    weaponId
        push    hitBodySide
        push    hitBodyPart
        push    dwPedInterface
        call    dwFunc
    }
    // clang-format on
}

CTaskComplexSunbatheSA::CTaskComplexSunbatheSA(CObject* pTowel, const bool bStartStanding)
{
    // TODO: Find out the real size
    CreateTaskInterface(1024);
    if (!IsValid())
        return;
    DWORD dwFunc = FUNC_CTaskComplexSunbathe__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();
    DWORD dwObjectInterface = 0;
    if (pTowel)
        dwObjectInterface = (DWORD)pTowel->GetObjectInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        push    bStartStanding
        push    dwObjectInterface;
        call    dwFunc
    }
    // clang-format on
}

void CTaskComplexSunbatheSA::SetEndTime(DWORD dwTime)
{
    CTaskComplexSunbatheSAInterface* thisInterface = (CTaskComplexSunbatheSAInterface*)GetInterface();
    thisInterface->m_BathingTimer.dwTimeEnd = dwTime;
}

////////////////////
// Player on foot //
////////////////////
CTaskSimplePlayerOnFootSA::CTaskSimplePlayerOnFootSA()
{
    CreateTaskInterface(sizeof(CTaskSimplePlayerOnFootSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = (DWORD)FUNC_CTASKSimplePlayerOnFoot__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
    }
    // clang-format on
}

////////////////////
// Complex facial //
////////////////////
CTaskComplexFacialSA::CTaskComplexFacialSA() : CTaskComplexFacialSA(true)
{
}

CTaskComplexFacialSA::CTaskComplexFacialSA(bool createNativeTask)
{
    // GTA owns the persistent secondary facial task. The task-management
    // system wraps that existing interface with false instead of allocating a
    // second controller whose requests would never reach the ped.
    if (!createNativeTask)
        return;

    CreateTaskInterface(sizeof(CTaskComplexFacialSAInterface));
    if (!IsValid())
        return;
    DWORD dwFunc = (DWORD)FUNC_CTASKComplexFacial__Constructor;
    DWORD dwThisInterface = (DWORD)GetInterface();

    // clang-format off
    __asm
    {
        mov     ecx, dwThisInterface
        call    dwFunc
    }
    // clang-format on
}

void CTaskComplexFacialSA::SetRequest(eFacialExpression typeA, int durationA, eFacialExpression typeB, int durationB)
{
    using SetRequest = void(__thiscall*)(CTaskComplexFacialSAInterface*, eFacialExpression, int, eFacialExpression, int);
    reinterpret_cast<SetRequest>(FUNC_CTASKComplexFacial__SetRequest)(static_cast<CTaskComplexFacialSAInterface*>(GetInterface()), typeA, durationA, typeB,
                                                                      durationB);
}

void CTaskComplexFacialSA::StopAll()
{
    using StopAll = void(__thiscall*)(CTaskComplexFacialSAInterface*);
    reinterpret_cast<StopAll>(FUNC_CTASKComplexFacial__StopAll)(static_cast<CTaskComplexFacialSAInterface*>(GetInterface()));
}

CTaskComplexInWaterSA::CTaskComplexInWaterSA()
{
    CreateTaskInterface(sizeof(CTaskComplexInWaterSA));
    if (!IsValid())
        return;

    // Call the constructor
    ((void(__thiscall*)(CTaskComplexInWaterSAInterface*))0x6350D0)(static_cast<CTaskComplexInWaterSAInterface*>(GetInterface()));
}
