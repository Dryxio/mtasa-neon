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
#include "CPedSA.h"

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
    // TODO: Find out the real size
    CreateTaskInterface(1024);
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
CTaskComplexFacialSA::CTaskComplexFacialSA()
{
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

CTaskComplexInWaterSA::CTaskComplexInWaterSA()
{
    CreateTaskInterface(sizeof(CTaskComplexInWaterSA));
    if (!IsValid())
        return;

    // Call the constructor
    ((void(__thiscall*)(CTaskComplexInWaterSAInterface*))0x6350D0)(static_cast<CTaskComplexInWaterSAInterface*>(GetInterface()));
}
