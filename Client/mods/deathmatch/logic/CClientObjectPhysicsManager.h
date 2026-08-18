/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CClientObjectPhysicsManager.h
 *  PURPOSE:     Opt-in native GTA physics for MTA-owned objects
 *
 *****************************************************************************/

#pragma once

#include "CClientObject.h"
#include "CClientObjectManager.h"
#include "../../../game_sa/CObjectSA.h"

#include <unordered_map>

class CClientObjectPhysicsManager
{
private:
    struct SState
    {
        CObject* pLastGameObject = nullptr;
        bool     bSnapshotValid = false;
        bool     bSleeping = false;
        unsigned char ucRestFrames = 0;
        unsigned char ucContactGraceFrames = 0;

        bool  bApplyGravity = false;
        bool  bDisableCollisionForce = false;
        bool  bCollidable = false;
        bool  bDisableTurnForce = false;
        bool  bDisableMoveForce = false;
        bool  bInfiniteMass = false;
        bool  bDisableZ = false;
        bool  bProcessCollisionEvenIfStationary = false;
        bool  bDontApplySpeed = false;
        bool  bForceHitReturnFalse = false;
        bool  bDisableSimpleCollision = false;
        bool  bCanBeCollidedWith = false;
        float fMass = 0.0f;
        float fTurnMass = 0.0f;
    };

    static inline std::unordered_map<CClientObject*, SState> ms_Objects;
    static inline unsigned int ms_uiLastPulseFrame = 0xFFFFFFFFu;

    // Network snapshots below these values are effectively rest and must not
    // wake an object GTA has already put to sleep.
    static constexpr float REST_LINEAR_SPEED_SQ = 0.003f * 0.003f;
    static constexpr float REST_TURN_SPEED_SQ = 0.003f * 0.003f;

    // GTA's discrete gravity + restitution can enter a tiny vertical limit
    // cycle on small elastic spheres. Once the object has had recent contact
    // and remains below these wider thresholds for several frames, finish the
    // same transition GTA's CObject::ProcessControl performs: zero all motion
    // and mark the object static.
    static constexpr float SETTLE_LINEAR_SPEED_SQ = 0.015f * 0.015f;
    static constexpr float SETTLE_TURN_SPEED_SQ = 0.050f * 0.050f;
    static constexpr unsigned char SETTLE_FRAMES = 12;
    static constexpr unsigned char CONTACT_GRACE_FRAMES = 6;

    static float LengthSq(const CVector& value)
    {
        return value.fX * value.fX + value.fY * value.fY + value.fZ * value.fZ;
    }

    static CObjectSA* GetObjectSA(CClientObject* pObject)
    {
        if (!pObject || !pObject->GetGameObject())
            return nullptr;
        return dynamic_cast<CObjectSA*>(pObject->GetGameObject());
    }

    static void Snapshot(CObjectSAInterface* pInterface, SState& state)
    {
        state.bApplyGravity = pInterface->bApplyGravity;
        // CPhysicalSAInterface still uses the historical MTA name bDisableFriction
        // for GTA's bDisableCollisionForce bit (0x4).
        state.bDisableCollisionForce = pInterface->bDisableFriction;
        state.bCollidable = pInterface->bCollidable;
        state.bDisableTurnForce = pInterface->b0x10;
        state.bDisableMoveForce = pInterface->bDisableMovement;
        state.bInfiniteMass = pInterface->b0x40;
        state.bDisableZ = pInterface->b0x80;
        state.bProcessCollisionEvenIfStationary = pInterface->b0x800;
        state.bDontApplySpeed = pInterface->bDontApplySpeed;
        state.bForceHitReturnFalse = pInterface->b0x10000;
        state.bDisableSimpleCollision = pInterface->b0x20000;
        state.bCanBeCollidedWith = pInterface->bEnableCollision;
        state.fMass = pInterface->m_fMass;
        state.fTurnMass = pInterface->m_fTurnMass;
        state.bSnapshotValid = true;
    }

    static void Wake(CClientObject* pObject, SState& state)
    {
        CObjectSA* pObjectSA = GetObjectSA(pObject);
        if (!pObjectSA)
            return;

        CObjectSAInterface* pInterface = pObjectSA->GetObjectInterface();
        if (!pInterface)
            return;

        state.bSleeping = false;
        state.ucRestFrames = 0;
        state.ucContactGraceFrames = 0;

        // Teleports and previous low-speed contacts can leave these flags in a
        // state that lets the next fast update skip collision work. A deliberate
        // wake must invalidate that state before adding the object back to GTA's
        // moving list.
        pObjectSA->SetStatic(false);
        pInterface->bCollisionProcessed = false;
        pInterface->bIsInSafePosition = false;
        pInterface->bIsStuck = false;
        pInterface->bOnSolidSurface = false;
        pInterface->m_ucColFlag1 = 0;
        pObjectSA->AddToMovingList();
    }

    static void PutToSleep(CClientObject* pObject, SState& state)
    {
        CObjectSA* pObjectSA = GetObjectSA(pObject);
        if (!pObjectSA)
            return;

        CObjectSAInterface* pInterface = pObjectSA->GetObjectInterface();
        if (!pInterface)
            return;

        const CVector zero;
        pInterface->m_vecLinearVelocity = zero;
        pInterface->m_vecAngularVelocity = zero;
        pInterface->m_vecCollisionLinearVelocity = zero;
        pInterface->m_vecCollisionAngularVelocity = zero;
        pObjectSA->SetStatic(true);

        pObject->m_vecMoveSpeed = zero;
        pObject->m_vecTurnSpeed = zero;
        state.bSleeping = true;
        state.ucRestFrames = 0;
        state.ucContactGraceFrames = 0;
    }

    static bool Apply(CClientObject* pObject, SState& state)
    {
        CObjectSA* pObjectSA = GetObjectSA(pObject);
        if (!pObjectSA)
        {
            state.pLastGameObject = nullptr;
            state.bSnapshotValid = false;
            return false;
        }

        CObjectSAInterface* pInterface = pObjectSA->GetObjectInterface();
        if (!pInterface)
            return false;

        state.pLastGameObject = pObject->GetGameObject();
        Snapshot(pInterface, state);

        pInterface->bApplyGravity = true;
        pInterface->bDisableFriction = false;
        pInterface->bCollidable = true;
        pInterface->b0x10 = false;
        pInterface->bDisableMovement = false;
        pInterface->b0x40 = false;
        pInterface->b0x80 = false;
        pInterface->b0x800 = true;
        pInterface->bDontApplySpeed = false;
        pInterface->b0x10000 = false;
        pInterface->b0x20000 = false;
        pInterface->bEnableCollision = true;

        // Dynamic physics deliberately does not manufacture collision or physical
        // properties. Resources must provide a valid COL and may tune mass,
        // elasticity, air resistance, etc. through the existing object APIs.
        pObjectSA->SetUsesCollision(true);
        pObjectSA->SetFrozen(pObject->IsFrozen());

        if (!pObject->IsFrozen())
        {
            if (state.bSleeping)
            {
                const CVector zero;
                pInterface->m_vecLinearVelocity = zero;
                pInterface->m_vecAngularVelocity = zero;
                pInterface->m_vecCollisionLinearVelocity = zero;
                pInterface->m_vecCollisionAngularVelocity = zero;
                pObjectSA->SetStatic(true);
            }
            else
            {
                Wake(pObject, state);

                // The server can send launch velocity before GTA has created the
                // streamed CObject. Restore the cached values into every new native
                // instance so streaming and syncer migration preserve momentum.
                pObjectSA->SetMoveSpeed(pObject->m_vecMoveSpeed);
                CVector vecTurnSpeed = pObject->m_vecTurnSpeed;
                pObjectSA->SetTurnSpeed(&vecTurnSpeed);
            }
        }

        return true;
    }

    static void CacheLiveState(CClientObject* pObject, SState& state)
    {
        CObjectSA* pObjectSA = GetObjectSA(pObject);
        if (!pObjectSA)
            return;

        CObjectSAInterface* pInterface = pObjectSA->GetObjectInterface();
        if (!pInterface)
            return;

        const CVector vecPosition = *pObjectSA->GetPosition();
        CVector       vecRotation;
        CVector       vecMoveSpeed;
        CVector       vecTurnSpeed;
        pObject->GetRotationRadians(vecRotation);
        pObjectSA->GetMoveSpeed(&vecMoveSpeed);
        pObjectSA->GetTurnSpeed(&vecTurnSpeed);

        if (!state.bSleeping && !pObject->IsFrozen())
        {
            const bool bContact = pInterface->bOnSolidSurface || pInterface->m_ucCollisionState != 0;
            if (bContact)
                state.ucContactGraceFrames = CONTACT_GRACE_FRAMES;
            else if (state.ucContactGraceFrames > 0)
                --state.ucContactGraceFrames;

            const bool bNearRest = LengthSq(vecMoveSpeed) <= SETTLE_LINEAR_SPEED_SQ && LengthSq(vecTurnSpeed) <= SETTLE_TURN_SPEED_SQ;
            if (bNearRest && state.ucContactGraceFrames > 0)
            {
                if (state.ucRestFrames < SETTLE_FRAMES)
                    ++state.ucRestFrames;
                if (state.ucRestFrames >= SETTLE_FRAMES)
                {
                    PutToSleep(pObject, state);
                    vecMoveSpeed = CVector();
                    vecTurnSpeed = CVector();
                }
            }
            else
            {
                state.ucRestFrames = 0;
            }
        }

        if (vecPosition != pObject->m_vecPosition)
        {
            pObject->m_vecPosition = vecPosition;
            pObject->UpdateStreamPosition(vecPosition);
        }
        pObject->m_vecRotation = vecRotation;
        pObject->m_vecMoveSpeed = vecMoveSpeed;
        pObject->m_vecTurnSpeed = vecTurnSpeed;
    }

    static void Restore(CClientObject* pObject, SState& state)
    {
        CObjectSA* pObjectSA = GetObjectSA(pObject);
        if (!pObjectSA || pObject->GetGameObject() != state.pLastGameObject || !state.bSnapshotValid)
            return;

        CObjectSAInterface* pInterface = pObjectSA->GetObjectInterface();
        if (!pInterface)
            return;

        pInterface->bApplyGravity = state.bApplyGravity;
        pInterface->bDisableFriction = state.bDisableCollisionForce;
        pInterface->bCollidable = state.bCollidable;
        pInterface->b0x10 = state.bDisableTurnForce;
        pInterface->bDisableMovement = state.bDisableMoveForce;
        pInterface->b0x40 = state.bInfiniteMass;
        pInterface->b0x80 = state.bDisableZ;
        pInterface->b0x800 = state.bProcessCollisionEvenIfStationary;
        pInterface->bDontApplySpeed = state.bDontApplySpeed;
        pInterface->b0x10000 = state.bForceHitReturnFalse;
        pInterface->b0x20000 = state.bDisableSimpleCollision;
        pInterface->bEnableCollision = state.bCanBeCollidedWith;
        pObjectSA->SetMass(state.fMass);
        pObjectSA->SetTurnMass(state.fTurnMass);
        pObjectSA->SetUsesCollision(pObject->IsCollisionEnabled());
    }

public:
    static bool SetEnabled(CClientObject* pObject, bool bEnabled)
    {
        if (!pObject)
            return false;

        if (!bEnabled)
        {
            auto iter = ms_Objects.find(pObject);
            if (iter == ms_Objects.end())
                return true;

            Restore(pObject, iter->second);
            ms_Objects.erase(iter);
            return true;
        }

        auto [iter, inserted] = ms_Objects.try_emplace(pObject);
        SState& state = iter->second;
        if (pObject->GetGameObject() != state.pLastGameObject || !state.bSnapshotValid)
            Apply(pObject, state);
        else if (!inserted)
            Wake(pObject, state);
        return true;
    }

    static bool IsEnabled(CClientObject* pObject)
    {
        return pObject && ms_Objects.contains(pObject);
    }

    static void ApplySyncedMoveSpeed(CClientObject* pObject, const CVector& vecMoveSpeed)
    {
        if (!pObject)
            return;

        const bool bNearRest = LengthSq(vecMoveSpeed) <= REST_LINEAR_SPEED_SQ;
        const CVector vecApplied = bNearRest ? CVector() : vecMoveSpeed;
        pObject->m_vecMoveSpeed = vecApplied;

        CObjectSA* pObjectSA = GetObjectSA(pObject);
        if (!pObjectSA)
            return;

        auto iter = ms_Objects.find(pObject);
        if (!bNearRest && iter != ms_Objects.end())
            Wake(pObject, iter->second);

        if (bNearRest)
        {
            // Directly update the native field. CPhysicalSA::SetMoveSpeed always
            // calls AddToMovingList + SetStatic(false), which would wake an object
            // that GTA had just put to sleep.
            pObjectSA->GetObjectInterface()->m_vecLinearVelocity = vecApplied;
        }
        else
        {
            pObjectSA->SetMoveSpeed(vecApplied);
        }
    }

    static void ApplySyncedTurnSpeed(CClientObject* pObject, const CVector& vecTurnSpeed)
    {
        if (!pObject)
            return;

        const bool bNearRest = LengthSq(vecTurnSpeed) <= REST_TURN_SPEED_SQ;
        const CVector vecApplied = bNearRest ? CVector() : vecTurnSpeed;
        pObject->m_vecTurnSpeed = vecApplied;

        CObjectSA* pObjectSA = GetObjectSA(pObject);
        if (!pObjectSA)
            return;

        auto iter = ms_Objects.find(pObject);
        if (!bNearRest && iter != ms_Objects.end())
            Wake(pObject, iter->second);

        if (bNearRest)
        {
            // Same rule as linear velocity: do not wake a sleeping object just
            // to apply a network snapshot that is already effectively zero.
            pObjectSA->GetObjectInterface()->m_vecAngularVelocity = vecApplied;
        }
        else
        {
            CVector vecAppliedCopy = vecApplied;
            pObjectSA->SetTurnSpeed(&vecAppliedCopy);
        }
    }

    static void Pulse()
    {
        if (!g_pClientGame || !g_pClientGame->GetManager())
            return;

        const unsigned int uiFrame = g_pClientGame->GetFrameCount();
        if (uiFrame == ms_uiLastPulseFrame)
            return;
        ms_uiLastPulseFrame = uiFrame;

        CClientObjectManager* pManager = g_pClientGame->GetManager()->GetObjectManager();
        if (!pManager)
            return;

        for (auto iter = ms_Objects.begin(); iter != ms_Objects.end();)
        {
            CClientObject* pObject = iter->first;
            if (!pManager->Exists(pObject))
            {
                iter = ms_Objects.erase(iter);
                continue;
            }

            if (pObject->GetGameObject() != iter->second.pLastGameObject)
            {
                iter->second.bSnapshotValid = false;
                Apply(pObject, iter->second);
            }

            if (pObject->GetGameObject() && pObject->GetGameObject() == iter->second.pLastGameObject)
                CacheLiveState(pObject, iter->second);

            ++iter;
        }
    }

    static void Shutdown()
    {
        ms_Objects.clear();
        ms_uiLastPulseFrame = 0xFFFFFFFFu;
    }
};
