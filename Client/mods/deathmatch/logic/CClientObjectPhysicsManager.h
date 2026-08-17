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
#include "CClientColModel.h"
#include "CClientColModelManager.h"
#include "CRuntimeColModel.h"
#include "../../../game_sa/CColModelSA.h"
#include "../../../game_sa/CModelInfoSA.h"
#include "../../../game_sa/CObjectSA.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>

class CClientObjectPhysicsManager
{
private:
    static constexpr unsigned short INVALID_FALLBACK_MODEL = 0xFFFF;
    static constexpr unsigned char  SMALLBOX_COLLISION_RESPONSE = 2;

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

        CObjectInfo* pOriginalObjectInfo = nullptr;
        CObjectInfo  objectInfoOverride{};
        bool         bObjectInfoOverride = false;

        unsigned short usFallbackModel = INVALID_FALLBACK_MODEL;
    };

    struct SFallbackCollision
    {
        std::unique_ptr<CClientColModel> pCollision;
        std::size_t                      users = 0;
    };

    static inline std::unordered_map<CClientObject*, SState>             ms_Objects;
    static inline std::unordered_map<unsigned short, SFallbackCollision> ms_FallbackCollisions;

    static CObjectSA* GetObjectSA(CClientObject* pObject)
    {
        if (!pObject || !pObject->GetGameObject())
            return nullptr;
        return dynamic_cast<CObjectSA*>(pObject->GetGameObject());
    }

    static bool HasCollisionVolumes(unsigned short usModel)
    {
        CModelInfo* pModelInfo = g_pGame ? g_pGame->GetModelInfo(usModel) : nullptr;
        if (!pModelInfo)
            return false;

        CBaseModelInfoSAInterface* pModelInterface = pModelInfo->GetInterface();
        CColModelSAInterface*      pColModel = pModelInterface ? pModelInterface->pColModel : nullptr;
        CColDataSA*                pData = pColModel ? pColModel->m_data : nullptr;
        return pData && (pData->m_numSpheres != 0 || pData->m_numBoxes != 0 || pData->m_numTriangles != 0);
    }

    static bool BuildFallbackCollision(unsigned short usModel, std::unique_ptr<CClientColModel>& outCollision)
    {
        if (!g_pClientGame || !g_pClientGame->GetManager() || !g_pGame)
            return false;

        CModelInfo* pModelInfo = g_pGame->GetModelInfo(usModel);
        if (!pModelInfo)
            return false;

        CVector center{0.0f, 0.0f, 0.0f};
        CVector size{1.0f, 0.25f, 0.20f};

        if (CBoundingBox* pBounds = pModelInfo->GetBoundingBox())
        {
            const CVector candidateCenter{
                (pBounds->vecBoundMin.fX + pBounds->vecBoundMax.fX) * 0.5f,
                (pBounds->vecBoundMin.fY + pBounds->vecBoundMax.fY) * 0.5f,
                (pBounds->vecBoundMin.fZ + pBounds->vecBoundMax.fZ) * 0.5f,
            };
            const CVector candidateSize{
                pBounds->vecBoundMax.fX - pBounds->vecBoundMin.fX,
                pBounds->vecBoundMax.fY - pBounds->vecBoundMin.fY,
                pBounds->vecBoundMax.fZ - pBounds->vecBoundMin.fZ,
            };

            if (std::isfinite(candidateCenter.fX) && std::isfinite(candidateCenter.fY) && std::isfinite(candidateCenter.fZ) &&
                std::isfinite(candidateSize.fX) && std::isfinite(candidateSize.fY) && std::isfinite(candidateSize.fZ) &&
                candidateSize.fX > 0.0f && candidateSize.fY > 0.0f && candidateSize.fZ > 0.0f)
            {
                center = candidateCenter;
                size = candidateSize;
            }
        }

        // Thin weapon bounds are easy to tunnel through at GTA's frame step.
        // Preserve the model dimensions while giving every axis a small, stable
        // collision thickness.
        size.fX = std::max(size.fX, 0.12f);
        size.fY = std::max(size.fY, 0.12f);
        size.fZ = std::max(size.fZ, 0.12f);

        RuntimeCollision::Model generated;

        // GTA's dynamic CPhysical collision path is sphere-driven. In
        // CCollision::ProcessColModels the moving model's spheres are tested
        // against world boxes/triangles; a fallback containing only a box can
        // therefore fall straight through triangle-based ground geometry.
        // Approximate the model with an overlapping sphere chain along its
        // longest axis while keeping the box for bounds/other collision tests.
        const float shortestAxis = std::min({size.fX, size.fY, size.fZ});
        const float sphereRadius = std::max(0.06f, shortestAxis * 0.30f);

        int   longestAxis = 0;
        float longestSize = size.fX;
        if (size.fY > longestSize)
        {
            longestAxis = 1;
            longestSize = size.fY;
        }
        if (size.fZ > longestSize)
        {
            longestAxis = 2;
            longestSize = size.fZ;
        }

        const float halfTravel = std::max(0.0f, longestSize * 0.5f - sphereRadius);
        std::size_t sphereCount = 1;
        if (halfTravel > 0.001f)
        {
            const float coveredLength = halfTravel * 2.0f;
            sphereCount = static_cast<std::size_t>(std::ceil(coveredLength / (sphereRadius * 2.0f))) + 1;
            sphereCount = std::clamp<std::size_t>(sphereCount, 3, 9);
        }

        for (std::size_t i = 0; i < sphereCount; ++i)
        {
            CVector sphereCenter = center;
            const float offset = sphereCount == 1 ? 0.0f : -halfTravel + (halfTravel * 2.0f * static_cast<float>(i)) / static_cast<float>(sphereCount - 1);
            if (longestAxis == 0)
                sphereCenter.fX += offset;
            else if (longestAxis == 1)
                sphereCenter.fY += offset;
            else
                sphereCenter.fZ += offset;

            generated.spheres.push_back(RuntimeCollision::Sphere{sphereCenter, sphereRadius, 0});
        }

        generated.boxes.push_back(RuntimeCollision::Box{center, size, 0});

        std::string buffer;
        std::string error;
        if (!RuntimeCollision::BuildCOLBuffer(generated, buffer, error))
        {
            if (g_pCore && g_pCore->GetConsole())
                g_pCore->GetConsole()->Printf("[dynamic-physics] model %u: fallback COL build failed: %s", usModel, error.c_str());
            return false;
        }

        auto pCollision = std::make_unique<CClientColModel>(g_pClientGame->GetManager(), INVALID_ELEMENT_ID);
        SString collisionData;
        collisionData.assign(buffer.data(), buffer.size());
        if (!pCollision->LoadGenerated(std::move(collisionData)) || !pCollision->Replace(usModel))
        {
            if (g_pCore && g_pCore->GetConsole())
                g_pCore->GetConsole()->Printf("[dynamic-physics] model %u: fallback COL load/replace failed", usModel);
            return false;
        }

        if (g_pCore && g_pCore->GetConsole())
        {
            g_pCore->GetConsole()->Printf(
                "[dynamic-physics] model %u: no native collision volumes; fallback %u spheres r=%.3f + box %.3f x %.3f x %.3f installed",
                usModel, static_cast<unsigned int>(sphereCount), sphereRadius, size.fX, size.fY, size.fZ);
        }

        outCollision = std::move(pCollision);
        return true;
    }

    static bool EnsureFallbackCollision(CClientObject* pObject, SState& state)
    {
        if (!pObject)
            return false;

        const unsigned short usModel = pObject->GetModel();

        if (state.usFallbackModel != INVALID_FALLBACK_MODEL && state.usFallbackModel != usModel)
            ReleaseFallbackCollision(state);

        if (state.usFallbackModel == usModel)
            return true;

        // Another dynamic object may already own the generated collision. Check
        // this before looking at ModelInfo, because the installed fallback now
        // appears as a normal collision model there.
        if (auto iter = ms_FallbackCollisions.find(usModel); iter != ms_FallbackCollisions.end())
        {
            ++iter->second.users;
            state.usFallbackModel = usModel;
            return true;
        }

        if (HasCollisionVolumes(usModel))
            return true;

        CClientColModelManager* pColManager = g_pClientGame && g_pClientGame->GetManager() ? g_pClientGame->GetManager()->GetColModelManager() : nullptr;
        if (!pColManager)
            return false;

        // Respect resource-owned engineReplaceCOL state. Dynamic physics only
        // fills genuinely missing native collision; it must not steal ownership
        // from a script replacement.
        if (pColManager->GetElementThatReplaced(usModel))
        {
            if (g_pCore && g_pCore->GetConsole())
                g_pCore->GetConsole()->Printf("[dynamic-physics] model %u: collision has no volumes, but a script COL replacement owns the model", usModel);
            return false;
        }

        SFallbackCollision fallback;
        if (!BuildFallbackCollision(usModel, fallback.pCollision))
            return false;

        fallback.users = 1;
        ms_FallbackCollisions.emplace(usModel, std::move(fallback));
        state.usFallbackModel = usModel;
        return true;
    }

    static void ReleaseFallbackCollision(SState& state)
    {
        if (state.usFallbackModel == INVALID_FALLBACK_MODEL)
            return;

        const unsigned short usModel = state.usFallbackModel;
        state.usFallbackModel = INVALID_FALLBACK_MODEL;

        auto iter = ms_FallbackCollisions.find(usModel);
        if (iter == ms_FallbackCollisions.end())
            return;

        if (iter->second.users > 1)
        {
            --iter->second.users;
            return;
        }

        // CClientColModel's destructor restores the original model collision if
        // our fallback still owns it. If a script replaced it after us, MTA has
        // already removed this model from our replacement list, so the script
        // replacement is left untouched.
        ms_FallbackCollisions.erase(iter);
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
        state.pOriginalObjectInfo = pInterface->pObjectInfo;
        state.bObjectInfoOverride = false;
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

        const bool bFallbackCollision = EnsureFallbackCollision(pObject, state) && state.usFallbackModel == pObject->GetModel();

        if (bFallbackCollision && pInterface->pObjectInfo)
        {
            state.objectInfoOverride = *pInterface->pObjectInfo;
            state.objectInfoOverride.ucSpecialColResponseCase = SMALLBOX_COLLISION_RESPONSE;
            pInterface->pObjectInfo = &state.objectInfoOverride;
            state.bObjectInfoOverride = true;
        }

        pInterface->bApplyGravity = true;
        pInterface->bDisableFriction = false;
        pInterface->bCollidable = true;
        pInterface->b0x10 = false;
        pInterface->bDisableMovement = false;
        pInterface->b0x40 = false;
        pInterface->b0x80 = false;
        pInterface->b0x800 = true;
        pInterface->b0x10000 = false;
        pInterface->b0x20000 = false;
        pInterface->bEnableCollision = true;

        // Weapon models and other models without object.dat entries are initialized
        // as effectively immovable (99999 mass/turn-mass). Give those models a sane
        // fallback while preserving explicit object properties configured by scripts.
        if (pInterface->m_fMass >= 99998.0f)
            pObjectSA->SetMass(4.0f);
        if (pInterface->m_fTurnMass >= 99998.0f)
            pObjectSA->SetTurnMass(8.0f);

        pObjectSA->SetUsesCollision(true);
        pObjectSA->SetFrozen(pObject->IsFrozen());

        if (!pObject->IsFrozen())
        {
            pObjectSA->SetStatic(false);

            // CObject was originally inserted into GTA's repeat sectors before
            // the generated fallback COL existed. Rebuild its sector links so
            // broad-phase collision sees the new bounds immediately. Calling the
            // entity virtuals directly deliberately leaves the moving-list link
            // alone; AddToMovingList below is idempotent.
            if (bFallbackCollision)
            {
                pInterface->Remove();
                pInterface->Add();
            }

            pInterface->bCollisionProcessed = false;
            pInterface->bIsInSafePosition = false;
            pInterface->bIsStuck = false;
            pObjectSA->AddToMovingList();

            // The server can send the toss velocity before GTA has created the
            // streamed CObject. CClientObject keeps those values cached, so
            // restore them into every newly-created native instance.
            pObjectSA->SetMoveSpeed(pObject->m_vecMoveSpeed);
            CVector vecTurnSpeed = pObject->m_vecTurnSpeed;
            pObjectSA->SetTurnSpeed(&vecTurnSpeed);
        }

        if (g_pCore && g_pCore->GetConsole())
        {
            CModelInfo* pModelInfo = g_pGame ? g_pGame->GetModelInfo(pObject->GetModel()) : nullptr;
            CBaseModelInfoSAInterface* pModelInterface = pModelInfo ? pModelInfo->GetInterface() : nullptr;
            CColModelSAInterface* pColModel = pModelInterface ? pModelInterface->pColModel : nullptr;
            CColDataSA* pData = pColModel ? pColModel->m_data : nullptr;
            const unsigned int spheres = pData ? pData->m_numSpheres : 0;
            const unsigned int boxes = pData ? pData->m_numBoxes : 0;
            const unsigned int triangles = pData ? pData->m_numTriangles : 0;
            const unsigned int specialResponse = pInterface->pObjectInfo ? pInterface->pObjectInfo->ucSpecialColResponseCase : 255;

            g_pCore->GetConsole()->Printf(
                "[dynamic-physics] model %u: status=%u static=%u moving=%u col=%u/%u/%u special=%u disableSimple=%u collisionForceOff=%u",
                pObject->GetModel(), static_cast<unsigned int>(pObjectSA->GetEntityStatus()), pObjectSA->IsStatic() ? 1u : 0u,
                pInterface->m_pMovingList ? 1u : 0u, spheres, boxes, triangles, specialResponse, pInterface->b0x20000 ? 1u : 0u,
                pInterface->bDisableFriction ? 1u : 0u);
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

        // Dynamic models such as weapons often have no object.dat group, so the
        // normal CClientObject::StreamedInPulse path deliberately skips them.
        // Keep the MTA element/streamer cache following the native GTA physics.
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
        if (state.bObjectInfoOverride && pInterface->pObjectInfo == &state.objectInfoOverride)
            pInterface->pObjectInfo = state.pOriginalObjectInfo;
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
            ReleaseFallbackCollision(iter->second);
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
                ReleaseFallbackCollision(iter->second);
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
        ms_FallbackCollisions.clear();
    }
};
