/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CPhysicalSA.h
 *  PURPOSE:     Header file for physical object entity base class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <game/CPhysical.h>
#include "CEntitySA.h"
#include <CVector.h>
#include "CPtrNodeDoubleListSA.h"

class CColPointSAInterface;

#define FUNC_GetMoveSpeed                   0x404460
#define FUNC_GetTurnSpeed                   0x470030
#define FUNC_ProcessCollision               0x54DFB0
#define FUNC_AttachEntityToEntity           0x54D570
#define FUNC_DetatchEntityFromEntity        0x5442F0
#define FUNC_CPhysical_AddToMovingList      0x542800
#define FUNC_CPhysical_RemoveFromMovingList 0x542860

#define PHYSICAL_MAXNOOFCOLLISIONRECORDS 6
#define PHYSICAL_MAXMASS                 99999.0

class CPhysicalSAInterface : public CEntitySAInterface
{
    virtual std::int32_t ProcessEntityCollision(CEntitySAInterface* entity, CColPointSAInterface* colPoint) = 0;

public:
    float  pad1;
    uint32 pad2;

    uint32 b0x01 : 1;
    uint32 bApplyGravity : 1;
    uint32 bDisableFriction : 1;
    uint32 bCollidable : 1;
    uint32 b0x10 : 1;
    uint32 bDisableMovement : 1;
    uint32 b0x40 : 1;
    uint32 b0x80 : 1;

    uint32 bSubmergedInWater : 1;
    uint32 bOnSolidSurface : 1;
    uint32 bBroken : 1;
    uint32 b0x800 : 1;
    uint32 b0x1000 : 1;
    uint32 bDontApplySpeed : 1;
    uint32 b0x4000 : 1;
    uint32 b0x8000 : 1;

    uint32 b0x10000 : 1;
    uint32 b0x20000 : 1;
    uint32 bBulletProof : 1;
    uint32 bFireProof : 1;
    uint32 bCollisionProof : 1;
    uint32 bMeeleProof : 1;
    uint32 bInvulnerable : 1;
    uint32 bExplosionProof : 1;

    uint32 b0x1000000 : 1;
    uint32 bAttachedToEntity : 1;
    uint32 b0x4000000 : 1;
    uint32 bTouchingWater : 1;
    uint32 bEnableCollision : 1;
    uint32 bDestroyed : 1;
    uint32 b0x40000000 : 1;
    uint32 b0x80000000 : 1;

    CVector                   m_vecLinearVelocity;
    CVector                   m_vecAngularVelocity;
    CVector                   m_vecCollisionLinearVelocity;
    CVector                   m_vecCollisionAngularVelocity;
    CVector                   m_vecOffsetUnk5;
    CVector                   m_vecOffsetUnk6;
    float                     m_fMass;
    float                     m_fTurnMass;
    float                     m_pad1;
    float                     m_fAirResistance;
    float                     m_fElasticity;
    float                     m_fBuoyancyConstant;
    CVector                   m_vecCenterOfMass;
    uint32*                   m_pCollisionList;
    uint32*                   m_pMovingList;
    uint8                     m_ucColFlag1;
    uint8                     m_ucCollisionState;
    uint8                     m_ucCollisionContactSurfaceType;
    uint8                     m_ucColFlag4;
    CEntity*                  pLastContactedEntity[4];
    float                     m_field_cc;
    float                     m_pad4c;
    float                     m_pad4d;
    float                     m_fDamageImpulseMagnitude;
    CEntitySAInterface*       m_pCollidedEntity;
    CVector                   m_vecCollisionImpactVelocity;
    CVector                   m_vecCollisionPosition;
    uint16                    m_usPieceType;
    uint16                    m_pad3;
    CEntitySAInterface*       m_pAttachedEntity;
    CVector                   m_vecAttachedOffset;
    CVector                   m_vecAttachedRotation;
    CVector                   m_vecUnk;
    uint32                    m_pad4;
    CPtrNodeDoubleLink<void>* m_pControlCodeNodeLink;
    float                     m_fLighting;
    float                     m_fLighting2;
    class CShadowDataSA*      m_pShadowData;

    CRect*      GetBoundRect_(CRect* pRect);
    static void StaticSetHooks();
};
static_assert(sizeof(CPhysicalSAInterface) == 0x138, "Invalid size for CPhysicalSAInterface");

class CPhysicalSA : public virtual CPhysical, public virtual CEntitySA
{
public:
    virtual void RestoreLastGoodPhysicsState();
    CVector*     GetMoveSpeed(CVector* vecMoveSpeed);
    CVector*     GetTurnSpeed(CVector* vecTurnSpeed);
    CVector*     GetMoveSpeedInternal(CVector* vecMoveSpeed);
    CVector*     GetTurnSpeedInternal(CVector* vecTurnSpeed);
    void         SetMoveSpeed(const CVector& vecMoveSpeed) noexcept;
    void         SetTurnSpeed(CVector* vecTurnSpeed);

    float GetMass();
    void  SetMass(float fMass);
    float GetTurnMass();
    void  SetTurnMass(float fTurnMass);
    float GetAirResistance();
    void  SetAirResistance(float fAirResistance);
    float GetElasticity();
    void  SetElasticity(float fElasticity);
    float GetBuoyancyConstant();
    void  SetBuoyancyConstant(float fBuoyancyConstant);
    void  GetCenterOfMass(CVector& vecCenterOfMass);
    void  SetCenterOfMass(CVector& vecCenterOfMass);

    void ProcessCollision();
    void AddToMovingList();

    float    GetDamageImpulseMagnitude();
    void     SetDamageImpulseMagnitude(float fMagnitude);
    CEntity* GetDamageEntity();
    void     SetDamageEntity(CEntity* pEntity);
    void     ResetLastDamage();

    CEntity* GetAttachedEntity();
    void     AttachEntityToEntity(CPhysical& Entity, const CVector& vecPosition, const CVector& vecRotation);
    void     DetachEntityFromEntity(float fUnkX, float fUnkY, float fUnkZ, bool bUnk);
    void     GetAttachedOffsets(CVector& vecPosition, CVector& vecRotation);
    void     SetAttachedOffsets(CVector& vecPosition, CVector& vecRotation);

    virtual bool InternalAttachEntityToEntity(DWORD dwEntityInterface, const CVector* vecPosition, const CVector* vecRotation);

    float GetLighting();
    void  SetLighting(float fLighting);

    void SetFrozen(bool bFrozen);
    SPhysicalProofs GetPhysicalProofs() const;
    void            SetPhysicalProofs(const SPhysicalProofs& proofs);
};
