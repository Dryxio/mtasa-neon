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
            pObjectSA->SetStatic(false);
            pInterface->bCollisionProcessed = false;
            pInterface->bIsInSafePosition = false;
            pInterface->bIsStuck = false;
            pObjectSA->AddToMovingList();

            // The server can send launch velocity before GTA has created the
            // streamed CObject. Restore the cached values into every new native
            // instance so streaming and syncer migration preserve momentum.
            pObjectSA->SetMoveSpeed(pObject->m_vecMoveSpeed);
            CVector vecTurnSpeed = pObject->m_vecTurnSpeed;
            pObjectSA->SetTurnSpeed(&vecTurnSpeed);
        }

        return true;
    }

    static void CacheLiveState(CClientObject* pObject)
    {
        CObjectSA* pObjectSA = GetObjectSA(pObject);
        if (!pObjectSA)
            return;

        const CVector vecPosition = *pObjectSA->GetPosition();
        CVector       vecRotation;
        CVector       vecMoveSpeed;
        CVector       vecTurnSpeed;
        pObject->GetRotationRadians(vecRotation);
        pObjectSA->GetMoveSpeed(&vecMoveSpeed);
        pObjectSA->GetTurnSpeed(&vecTurnSpeed);

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

        SState& state = ms_Objects[pObject];
        if (pObject->GetGameObject() != state.pLastGameObject || !state.bSnapshotValid)
            Apply(pObject, state);
        return true;
    }

    static bool IsEnabled(CClientObject* pObject)
    {
        return pObject && ms_Objects.contains(pObject);
    }

    static void Pulse()
    {
        if (!g_pClientGame || !g_pClientGame->GetManager())
            return;

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
                CacheLiveState(pObject);

            ++iter;
        }
    }

    static void Shutdown()
    {
        ms_Objects.clear();
    }
};