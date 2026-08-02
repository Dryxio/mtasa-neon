/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/TaskJumpFallSA.h
 *  PURPOSE:     Jump and fall game tasks
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <CVector.h>
#include <game/TaskJumpFall.h>
#include <game/CTasks.h>
#include "TaskSA.h"

// temporary
class CAnimBlendAssociation;
class CAnimBlendAssociationSAInterface;
class FxSystem_c;

#define FUNC_CTaskSimpleClimb__Constructor         0x67A110
#define FUNC_CTaskSimpleJetPack__Constructor       0x67B4E0
#define FUNC_CTaskComplexJump__Constructor         0x67A030
#define FUNC_CTaskComplexInAirAndLand__Constructor 0x678C80

// Neon only needs to construct this task. GTA retains ownership of its native
// jump, airborne, optional climb, and landing state machine.
class CTaskComplexJumpSAInterface : public CTaskComplexSAInterface
{
private:
    int           m_iForceClimb;
    unsigned char m_bUsePlayerLaunchForce;
    unsigned char m_pad[3];
};
static_assert(sizeof(CTaskComplexJumpSAInterface) == 0x14, "Unexpected CTaskComplexJumpSAInterface size");

class CTaskComplexJumpSA : public virtual CTaskComplexSA
{
public:
    CTaskComplexJumpSA() {};
    explicit CTaskComplexJumpSA(bool bAllowClimb);
};

class CTaskComplexInAirAndLandSAInterface : public CTaskComplexSAInterface
{
private:
    bool         m_bUsingJumpGlide;
    bool         m_bUsingFallGlide;
    bool         m_bInvalidClimb;
    std::uint8_t m_pad;
};
static_assert(sizeof(CTaskComplexInAirAndLandSAInterface) == 0x10, "Unexpected CTaskComplexInAirAndLandSAInterface size");

class CTaskComplexInAirAndLandSA : public virtual CTaskComplexSA
{
public:
    CTaskComplexInAirAndLandSA() {};
    CTaskComplexInAirAndLandSA(bool bUsingJumpGlide, bool bUsingFallGlide);
};

// GTA owns the entire airborne state machine. These wrappers expose only the
// live associations selected by its simple subtasks so remote clients can
// present the same launch, glide, landing, or blocked-jump response.
class CTaskSimpleJumpSAInterface : public CTaskSimpleSAInterface
{
public:
    CVector                           m_vecClimbPosition;
    float                             m_fClimbAngle;
    std::uint8_t                      m_ucClimbSurfaceType;
    std::uint8_t                      m_padSurface[3];
    CEntitySAInterface*               m_pClimbEntity;
    bool                              m_bIsFinished;
    bool                              m_bIsJumpBlocked;
    bool                              m_bClimbJump;
    bool                              m_bLaunchAnimationStarted;
    bool                              m_bCanClimb;
    bool                              m_bHighJump;
    std::uint8_t                      m_padAnimation[2];
    CAnimBlendAssociationSAInterface* m_pAnim;
};
static_assert(sizeof(CTaskSimpleJumpSAInterface) == 0x2C, "Unexpected CTaskSimpleJumpSAInterface size");
static_assert(offsetof(CTaskSimpleJumpSAInterface, m_pAnim) == 0x28, "Invalid simple-jump animation offset");

class CTaskSimpleJumpSA : public virtual CTaskSimpleSA
{
public:
    CTaskSimpleJumpSA() {};

    bool GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed, float& fBlendAmount) const override;
};

class CTaskSimpleInAirSAInterface : public CTaskSimpleSAInterface
{
public:
    CVector                           m_vecPosition;
    float                             m_fAngle;
    std::uint8_t                      m_ucSurfaceType;
    std::uint8_t                      m_padSurface[3];
    CAnimBlendAssociationSAInterface* m_pAnim;
    float                             m_fMinimumVerticalSpeed;
    std::uint8_t                      m_ucFlags;
    std::uint8_t                      m_padCounter[3];
    std::uint32_t                     m_uiProcessCounter;
    std::uint8_t                      m_timer[10];
    std::uint8_t                      m_padTimer[2];
    CEntitySAInterface*               m_pClimbEntity;
};
static_assert(sizeof(CTaskSimpleInAirSAInterface) == 0x3C, "Unexpected CTaskSimpleInAirSAInterface size");
static_assert(offsetof(CTaskSimpleInAirSAInterface, m_pAnim) == 0x1C, "Invalid in-air animation offset");

class CTaskSimpleInAirSA : public virtual CTaskSimpleSA
{
public:
    CTaskSimpleInAirSA() {};

    bool GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed, float& fBlendAmount) const override;
};

class CTaskSimpleLandSAInterface : public CTaskSimpleSAInterface
{
public:
    CAnimBlendAssociationSAInterface* m_pAnim;
    std::int32_t                      m_iAnimId;
    std::uint8_t                      m_ucFlags;
    std::uint8_t                      m_pad[3];
};
static_assert(sizeof(CTaskSimpleLandSAInterface) == 0x14, "Unexpected CTaskSimpleLandSAInterface size");
static_assert(offsetof(CTaskSimpleLandSAInterface, m_pAnim) == 0x08, "Invalid landing animation offset");

class CTaskSimpleLandSA : public virtual CTaskSimpleSA
{
public:
    CTaskSimpleLandSA() {};

    bool GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed, float& fBlendAmount) const override;
};

class CTaskSimpleHitHeadSAInterface : public CTaskSimpleSAInterface
{
public:
    bool                              m_bIsFinished;
    std::uint8_t                      m_pad[3];
    CAnimBlendAssociationSAInterface* m_pAnim;
};
static_assert(sizeof(CTaskSimpleHitHeadSAInterface) == 0x10, "Unexpected CTaskSimpleHitHeadSAInterface size");
static_assert(offsetof(CTaskSimpleHitHeadSAInterface, m_pAnim) == 0x0C, "Invalid hit-head animation offset");

class CTaskSimpleHitHeadSA : public virtual CTaskSimpleSA
{
public:
    CTaskSimpleHitHeadSA() {};

    bool GetPresentationAnimation(unsigned short& usAnimGroup, unsigned short& usAnimId, float& fProgress, float& fSpeed, float& fBlendAmount) const override;
};

class CTaskSimpleClimbSAInterface : public CTaskSimpleSAInterface
{
public:
    bool                m_bIsFinished;
    bool                m_bChangeAnimation;
    bool                m_bChangePosition;
    bool                m_bForceClimb;
    bool                m_bInvalidClimb;
    eClimbHeights       m_nHeightForAnim;
    eClimbHeights       m_nHeightForPos;
    unsigned char       m_nSurfaceType;
    char                m_nFallAfterVault;
    float               m_fHandholdHeading;
    CVector             m_vecHandholdPos;
    CEntitySAInterface* m_pClimbEnt;

    short                  m_nGetToPosCounter;
    CAnimBlendAssociation* m_pAnim;
};

class CTaskSimpleClimbSA : public virtual CTaskSimpleSA, public virtual CTaskSimpleClimb
{
public:
    CTaskSimpleClimbSA() {};
    CTaskSimpleClimbSA(CEntitySAInterface* pClimbEnt, const CVector& vecTarget, float fHeading, unsigned char nSurfaceType, eClimbHeights nHeight = CLIMB_GRAB,
                       const bool bForceClimb = false);

    eClimbHeights GetHeightForPos() const override { return static_cast<const CTaskSimpleClimbSAInterface*>(GetInterface())->m_nHeightForPos; }
};

// ##############################################################################
// ## Name:    CTaskSimpleJetPack
// ## Purpose: Allows the player to use a jetpack to 'fly' around
// ##############################################################################

class CTaskSimpleJetPackSAInterface : public CTaskSimpleSAInterface
{
public:
    bool          m_bIsFinished;
    unsigned char m_bAddedIdleAnim;
    unsigned char m_bAnimsReferenced;
    unsigned char m_bAttackButtonPressed;
    unsigned char m_bSwitchedWeapons;

    char  m_nThrustStop;
    char  m_nThrustFwd;
    float m_fThrustStrafe;
    float m_fThrustAngle;

    float m_fLegSwingFwd;
    float m_fLegSwingSide;
    float m_fLegTwist;

    float m_fLegSwingFwdSpeed;
    float m_fLegSwingSideSpeed;
    float m_fLegTwistSpeed;

    CVector m_vecOldSpeed;
    float   m_fOldHeading;

    RpClump*               m_pJetPackClump;
    CAnimBlendAssociation* m_pAnim;

    CVector      m_vecTargetPos;
    float        m_fCruiseHeight;
    int          m_nHoverTime;
    unsigned int m_nStartHover;
    CEntity*     m_pTargetEnt;

    FxSystem_c* m_pFxSysL;
    FxSystem_c* m_pFxSysR;
    float       m_fxKeyTime;
};

class CTaskSimpleJetPackSA : public virtual CTaskSimpleSA, public virtual CTaskSimpleJetPack
{
public:
    CTaskSimpleJetPackSA() {};
    CTaskSimpleJetPackSA(const CVector* pVecTargetPos, float fCruiseHeight = 10.0f, int nHoverTime = 0);

    bool IsFinished() const override { return static_cast<const CTaskSimpleJetPackSAInterface*>(GetInterface())->m_bIsFinished; }
};
