/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskPhysicalResponseSA.h
 *  PURPOSE:     Physical response game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <game/TaskPhysicalResponse.h>
#include "TaskSA.h"

#define FUNC_CTaskSimpleChoking__Constructor 0x6202C0
#define FUNC_CTaskSimpleChoking__UpdateChoke 0x620660

class CAnimBlendAssociationSAInterface;

class CTaskSimpleFallSAInterface : public CTaskSimpleSAInterface
{
public:
    bool                              m_bIsFinished;
    unsigned char                     m_pad[3];
    std::int32_t                      m_iAnimId;
    std::int32_t                      m_iAnimGroup;
    CAnimBlendAssociationSAInterface* m_pAnim;
    std::int32_t                      m_iTotalDownTime;
    std::uint32_t                     m_uiCurrentDownTime;
};
static_assert(sizeof(CTaskSimpleFallSAInterface) == 0x20, "Unexpected CTaskSimpleFallSAInterface size");
static_assert(offsetof(CTaskSimpleFallSAInterface, m_pAnim) == 0x14, "Invalid fall animation offset");

class CTaskSimpleFallSA : public virtual CTaskSimpleSA
{
public:
    CTaskSimpleFallSA() {};

    bool GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed, float& fBlendAmount) const override;
};

class CTaskSimpleGetUpSAInterface : public CTaskSimpleSAInterface
{
public:
    bool                              m_bHasPedGotUp;
    bool                              m_bIsFinished;
    unsigned char                     m_pad[2];
    CAnimBlendAssociationSAInterface* m_pAnim;
};
static_assert(sizeof(CTaskSimpleGetUpSAInterface) == 0x10, "Unexpected CTaskSimpleGetUpSAInterface size");
static_assert(offsetof(CTaskSimpleGetUpSAInterface, m_pAnim) == 0x0C, "Invalid get-up animation offset");

class CTaskSimpleGetUpSA : public virtual CTaskSimpleSA
{
public:
    CTaskSimpleGetUpSA() {};

    bool GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed, float& fBlendAmount) const override;
};

class CTaskSimpleChokingSAInterface : public CTaskSimpleSAInterface
{
public:
    class CPedSAInterface* m_pAttacker;
    DWORD*                 m_pAnim;
    unsigned int           m_nTimeRemaining;
    unsigned int           m_nTimeStarted;
    unsigned char          m_bIsTeargas;
    bool                   m_bIsFinished;
};

class CTaskSimpleChokingSA : public virtual CTaskSimpleSA, public virtual CTaskSimpleChoking
{
public:
    CTaskSimpleChokingSA() {};
    CTaskSimpleChokingSA(CPed* pAttacker, bool bIsTearGas);

    CPed*        GetAttacker();
    unsigned int GetTimeRemaining();
    unsigned int GetTimeStarted();
    bool         IsTeargas();
    bool         IsFinished();

    void UpdateChoke(CPed* pPed, CPed* pAttacker, bool bIsTearGas);
};
