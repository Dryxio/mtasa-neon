/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Managed rope presentation and native-slot leasing
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CClientRopeManager.h"
#include "CClientDummy.h"
#include "CClientManager.h"
#include "CDeathmatchObject.h"
#include "CDeathmatchVehicle.h"
#include "CStaticFunctionDefinitions.h"
#ifdef WITH_OBJECT_SYNC
    #include "CObjectSync.h"
#endif
#include <game/CRopes.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float ACQUIRE_DISTANCE = 170.0f;
constexpr float RELEASE_DISTANCE = 210.0f;

bool GetNumber(CClientEntity* pElement, const char* szKey, double& dValue)
{
    CLuaArgument* pArgument = pElement ? pElement->GetCustomData(szKey, false) : nullptr;
    if (!pArgument || pArgument->GetType() != LUA_TNUMBER)
        return false;
    dValue = pArgument->GetNumber();
    return true;
}

bool GetBool(CClientEntity* pElement, const char* szKey, bool& bValue)
{
    CLuaArgument* pArgument = pElement ? pElement->GetCustomData(szKey, false) : nullptr;
    if (!pArgument || pArgument->GetType() != LUA_TBOOLEAN)
        return false;
    bValue = pArgument->GetBoolean();
    return true;
}

CClientEntity* GetElement(CClientEntity* pElement, const char* szKey)
{
    CLuaArgument* pArgument = pElement ? pElement->GetCustomData(szKey, false) : nullptr;
    return pArgument ? pArgument->GetElement() : nullptr;
}

void SetLocalNumber(CClientEntity* pElement, const char* szKey, double dValue)
{
    CLuaArgument argument;
    argument.ReadNumber(dValue);
    pElement->SetCustomData(szKey, argument, false);
}

float DistanceSquared(const CVector& a, const CVector& b)
{
    const float x = a.fX - b.fX;
    const float y = a.fY - b.fY;
    const float z = a.fZ - b.fZ;
    return x * x + y * y + z * z;
}
}  // namespace

CClientRopeManager::CClientRopeManager(CClientManager* pManager) : m_pManager(pManager)
{
}

CClientRopeManager::~CClientRopeManager()
{
    for (auto& rope : m_Ropes)
        ReleaseLease(rope);
    m_Ropes.clear();
}

bool CClientRopeManager::IsRopeElement(const CClientEntity* pElement)
{
    return pElement && pElement->GetType() == CCLIENTDUMMY && const_cast<CClientEntity*>(pElement)->GetTypeName() == "rope";
}

void CClientRopeManager::Register(CClientDummy* pRope)
{
    if (!pRope || !IsRopeElement(pRope) || FindEntry(pRope))
        return;

    SRopeEntry entry;
    entry.pElement = pRope;
    entry.ullLastPulse = GetTickCount64_();
    m_Ropes.push_back(entry);
}

void CClientRopeManager::Unregister(CClientDummy* pRope)
{
    auto iter = std::find_if(m_Ropes.begin(), m_Ropes.end(), [pRope](const SRopeEntry& entry) { return entry.pElement == pRope; });
    if (iter == m_Ropes.end())
        return;

    ReleaseLease(*iter);
    m_Ropes.erase(iter);
}

CClientRopeManager::SRopeEntry* CClientRopeManager::FindEntry(const CClientEntity* pElement)
{
    auto iter = std::find_if(m_Ropes.begin(), m_Ropes.end(), [pElement](const SRopeEntry& entry) { return entry.pElement == pElement; });
    return iter != m_Ropes.end() ? &*iter : nullptr;
}

const CClientRopeManager::SRopeEntry* CClientRopeManager::FindEntry(const CClientEntity* pElement) const
{
    auto iter = std::find_if(m_Ropes.begin(), m_Ropes.end(), [pElement](const SRopeEntry& entry) { return entry.pElement == pElement; });
    return iter != m_Ropes.end() ? &*iter : nullptr;
}

bool CClientRopeManager::IsActive(const CClientEntity* pElement) const
{
    const SRopeEntry* pEntry = FindEntry(pElement);
    return pEntry && pEntry->bLeased && g_pGame->GetRopes()->FindRope(pEntry->uiNativeId) >= 0;
}

bool CClientRopeManager::GetPositionAt(const CClientEntity* pElement, float fProgress, CVector& vecPosition, CVector* pVelocity) const
{
    const SRopeEntry* pEntry = FindEntry(pElement);
    return pEntry && pEntry->bLeased && g_pGame->GetRopes()->FindCoorsAlongRope(pEntry->uiNativeId, fProgress, vecPosition, pVelocity);
}

bool CClientRopeManager::ResolveAnchor(CClientDummy* pRope, CVector& vecAnchor, CClientEntity*& pHolder) const
{
    if (!pRope)
        return false;

    pRope->GetPosition(vecAnchor);
    pHolder = GetElement(pRope, KEY_HOLDER);
    if (!pHolder || pHolder->IsBeingDeleted())
    {
        pHolder = nullptr;
        return true;
    }

    CVector holderPosition;
    pHolder->GetPosition(holderPosition);

    double x = 0.0, y = 0.0, z = 0.0;
    GetNumber(pRope, KEY_OFFSET_X, x);
    GetNumber(pRope, KEY_OFFSET_Y, y);
    GetNumber(pRope, KEY_OFFSET_Z, z);
    CVector offset(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

    CMatrix matrix;
    if (pHolder->GetMatrix(matrix))
        offset = matrix.TransformVector(offset);

    vecAnchor = CVector(holderPosition.fX + offset.fX, holderPosition.fY + offset.fY, holderPosition.fZ + offset.fZ);
    return true;
}

bool CClientRopeManager::ShouldOwnPhysics(CClientEntity* pEntity) const
{
    if (!pEntity)
        return true;
    if (pEntity->IsLocalEntity())
        return true;

    if (pEntity->GetType() == CCLIENTVEHICLE)
    {
        auto* pVehicle = dynamic_cast<CDeathmatchVehicle*>(pEntity);
        return pVehicle && pVehicle->IsSyncing();
    }

    if (pEntity->GetType() == CCLIENTOBJECT)
    {
#ifdef WITH_OBJECT_SYNC
        auto* pObject = dynamic_cast<CDeathmatchObject*>(pEntity);
        return pObject && g_pClientGame && g_pClientGame->GetObjectSync() && g_pClientGame->GetObjectSync()->Exists(pObject);
#else
        return false;
#endif
    }

    return false;
}

std::uint32_t CClientRopeManager::AllocateNativeId()
{
    for (unsigned int attempt = 0; attempt < 0x10000u; ++attempt)
    {
        const std::uint32_t candidate = m_uiNextNativeId++;
        if (!candidate)
            continue;
        if (g_pGame->GetRopes()->FindRope(candidate) < 0)
            return candidate;
    }
    return 0;
}

bool CClientRopeManager::UpdateNative(SRopeEntry& entry, const CVector& vecAnchor, CClientEntity* pHolder)
{
    CClientDummy* pRope = entry.pElement;
    if (!pRope || !entry.uiNativeId)
        return false;

    double typeValue = static_cast<double>(static_cast<unsigned int>(eRopeType::SWAT));
    double fixedNodeValue = 0.0;
    double winchHeight = 0.5;
    double length = 0.0;
    GetNumber(pRope, KEY_TYPE, typeValue);
    GetNumber(pRope, KEY_FIXED_NODE, fixedNodeValue);
    GetNumber(pRope, KEY_WINCH_HEIGHT, winchHeight);
    GetNumber(pRope, KEY_LENGTH, length);

    const int typeNumber = std::clamp(static_cast<int>(typeValue), 1, 8);
    const auto ropeType = static_cast<eRopeType>(typeNumber);
    const std::uint8_t fixedNode = static_cast<std::uint8_t>(std::clamp(static_cast<int>(fixedNodeValue), 0, 30));

    bool sitOnGround = false;
    bool physics = true;
    GetBool(pRope, KEY_SIT_ON_GROUND, sitOnGround);
    GetBool(pRope, KEY_PHYSICS, physics);

    // A non-syncer may still render and simulate a rope, but it must not give GTA
    // a physical holder pointer because CRope::Update applies reaction forces to it.
    CEntitySAInterface* pNativeHolder = nullptr;
    if (physics && pHolder && ShouldOwnPhysics(pHolder))
    {
        CEntity* pGameEntity = pHolder->GetGameEntity();
        pNativeHolder = pGameEntity ? pGameEntity->GetInterface() : nullptr;
    }

    if (!g_pGame->GetRopes()->RegisterRope(entry.uiNativeId, ropeType, vecAnchor, false, fixedNode, sitOnGround, pNativeHolder, 20000))
        return false;

    double vx = 0.0, vy = 0.0, vz = 0.0;
    GetNumber(pRope, KEY_VELOCITY_X, vx);
    GetNumber(pRope, KEY_VELOCITY_Y, vy);
    GetNumber(pRope, KEY_VELOCITY_Z, vz);
    g_pGame->GetRopes()->SetSpeedOfTopNode(entry.uiNativeId, CVector(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)));

    if (winchHeight >= 0.01)
        g_pGame->GetRopes()->SetWinchHeight(entry.uiNativeId, static_cast<float>(winchHeight));
    if (length > 0.0)
        g_pGame->GetRopes()->SetRopeLength(entry.uiNativeId, static_cast<float>(length));

    CClientEntity* pCarried = GetElement(pRope, KEY_CARRIED);
    const bool carriedTypeAllowed = pCarried && (pCarried->GetType() == CCLIENTOBJECT || pCarried->GetType() == CCLIENTVEHICLE);
    const bool canOwnCarried = physics && carriedTypeAllowed && ShouldOwnPhysics(pCarried) && (!pHolder || ShouldOwnPhysics(pHolder));

    CEntitySAInterface* pNativeCarried = nullptr;
    if (canOwnCarried)
    {
        CEntity* pGameEntity = pCarried->GetGameEntity();
        pNativeCarried = pGameEntity ? pGameEntity->GetInterface() : nullptr;
    }

    CEntitySAInterface* pCurrentCarried = g_pGame->GetRopes()->GetPickedUpEntity(entry.uiNativeId);
    if (pNativeCarried && pNativeCarried != pCurrentCarried)
    {
        if (pCurrentCarried)
            g_pGame->GetRopes()->ReleasePickedUpEntity(entry.uiNativeId);
        entry.bPhysicsAttached = g_pGame->GetRopes()->PickUpEntity(entry.uiNativeId, pNativeCarried);
    }
    else if (!pNativeCarried && pCurrentCarried)
    {
        g_pGame->GetRopes()->ReleasePickedUpEntity(entry.uiNativeId);
        entry.bPhysicsAttached = false;
    }
    else
    {
        entry.bPhysicsAttached = pNativeCarried && pCurrentCarried == pNativeCarried;
    }

    return true;
}

bool CClientRopeManager::AcquireLease(SRopeEntry& entry, const CVector& vecAnchor, CClientEntity* pHolder)
{
    if (entry.bLeased)
        return UpdateNative(entry, vecAnchor, pHolder);
    if (!g_pGame || !g_pGame->GetRopes() || g_pGame->GetRopes()->GetFreeRopeCount() == 0)
        return false;

    entry.uiNativeId = AllocateNativeId();
    if (!entry.uiNativeId)
        return false;

    if (!UpdateNative(entry, vecAnchor, pHolder) || g_pGame->GetRopes()->FindRope(entry.uiNativeId) < 0)
    {
        entry.uiNativeId = 0;
        return false;
    }

    entry.bLeased = true;
    return true;
}

void CClientRopeManager::ReleaseLease(SRopeEntry& entry)
{
    if (!entry.uiNativeId || !g_pGame || !g_pGame->GetRopes())
    {
        entry.bLeased = false;
        entry.bPhysicsAttached = false;
        entry.uiNativeId = 0;
        return;
    }

    g_pGame->GetRopes()->ReleasePickedUpEntity(entry.uiNativeId);
    g_pGame->GetRopes()->RemoveRope(entry.uiNativeId);
    entry.bLeased = false;
    entry.bPhysicsAttached = false;
    entry.uiNativeId = 0;
}

void CClientRopeManager::DoPulse()
{
    if (!m_pManager || !g_pGame || !g_pGame->GetRopes())
        return;

    CClientPlayer* pLocalPlayer = m_pManager->GetPlayerManager()->GetLocalPlayer();
    if (!pLocalPlayer)
        return;

    CVector cameraPosition;
    m_pManager->GetCamera()->GetPosition(cameraPosition);
    const unsigned short dimension = pLocalPlayer->GetDimension();
    const unsigned char interior = pLocalPlayer->GetInterior();
    const unsigned long long now = GetTickCount64_();

    struct SCandidate
    {
        SRopeEntry* pEntry{};
        CVector     anchor;
        CClientEntity* pHolder{};
        float       distanceSq{};
        bool        physicsCritical{};
    };

    std::vector<SCandidate> candidates;
    std::vector<CClientDummy*> expiredLocalRopes;

    for (auto& entry : m_Ropes)
    {
        CClientDummy* pRope = entry.pElement;
        if (!pRope || pRope->IsBeingDeleted())
            continue;

        const unsigned long long elapsed = entry.ullLastPulse ? now - entry.ullLastPulse : 0;
        entry.ullLastPulse = now;
        double duration = 0.0;
        double remaining = 0.0;
        GetNumber(pRope, KEY_DURATION, duration);
        GetNumber(pRope, KEY_REMAINING, remaining);
        if (duration > 0.0 && remaining > 0.0 && elapsed > 0)
        {
            remaining = std::max(0.0, remaining - static_cast<double>(elapsed));
            SetLocalNumber(pRope, KEY_REMAINING, remaining);
        }
        if (duration > 0.0 && remaining <= 0.0)
        {
            ReleaseLease(entry);
            if (pRope->IsLocalEntity())
                expiredLocalRopes.push_back(pRope);
            continue;
        }

        CVector anchor;
        CClientEntity* pHolder = nullptr;
        if (!ResolveAnchor(pRope, anchor, pHolder))
        {
            ReleaseLease(entry);
            continue;
        }

        const bool contextMatches = pRope->GetDimension() == dimension && pRope->GetInterior() == interior;
        const float distanceSq = DistanceSquared(cameraPosition, anchor);
        bool physics = true;
        GetBool(pRope, KEY_PHYSICS, physics);
        CClientEntity* pCarried = GetElement(pRope, KEY_CARRIED);
        const bool physicsCritical = physics && pCarried && ShouldOwnPhysics(pCarried) && (!pHolder || ShouldOwnPhysics(pHolder));

        if (!contextMatches || (!physicsCritical && distanceSq > RELEASE_DISTANCE * RELEASE_DISTANCE))
        {
            ReleaseLease(entry);
            continue;
        }

        if (entry.bLeased)
        {
            if (!UpdateNative(entry, anchor, pHolder))
                ReleaseLease(entry);
            continue;
        }

        if (physicsCritical || distanceSq <= ACQUIRE_DISTANCE * ACQUIRE_DISTANCE)
            candidates.push_back({&entry, anchor, pHolder, distanceSq, physicsCritical});
    }

    std::sort(candidates.begin(), candidates.end(), [](const SCandidate& a, const SCandidate& b) {
        if (a.physicsCritical != b.physicsCritical)
            return a.physicsCritical > b.physicsCritical;
        return a.distanceSq < b.distanceSq;
    });

    for (SCandidate& candidate : candidates)
    {
        if (g_pGame->GetRopes()->GetFreeRopeCount() == 0)
            break;
        AcquireLease(*candidate.pEntry, candidate.anchor, candidate.pHolder);
    }

    for (CClientDummy* pRope : expiredLocalRopes)
    {
        if (pRope && !pRope->IsBeingDeleted())
            CStaticFunctionDefinitions::DestroyElement(*pRope);
    }
}
