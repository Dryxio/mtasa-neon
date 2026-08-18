/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Managed fire presentation and gameplay policy
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CClientFireManager.h"
#include "CClientDummy.h"
#include "CClientManager.h"
#include <game/CFxManager.h>
#include <game/CFxSystem.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
constexpr unsigned long long DAMAGE_INTERVAL_MS = 750;

bool GetNumber(CClientEntity* pElement, const char* szKey, double& dValue)
{
    if (!pElement)
        return false;

    CLuaArgument* pArgument = pElement->GetCustomData(szKey, false);
    if (!pArgument || pArgument->GetType() != LUA_TNUMBER)
        return false;

    dValue = pArgument->GetNumber();
    return true;
}

bool GetBool(CClientEntity* pElement, const char* szKey, bool& bValue)
{
    if (!pElement)
        return false;

    CLuaArgument* pArgument = pElement->GetCustomData(szKey, false);
    if (!pArgument || pArgument->GetType() != LUA_TBOOLEAN)
        return false;

    bValue = pArgument->GetBoolean();
    return true;
}

CClientEntity* GetElement(CClientEntity* pElement, const char* szKey)
{
    if (!pElement)
        return nullptr;

    CLuaArgument* pArgument = pElement->GetCustomData(szKey, false);
    return pArgument ? pArgument->GetElement() : nullptr;
}

int GetFxTier(float fStrength)
{
    if (fStrength <= 1.0f)
        return 0;
    if (fStrength <= 2.0f)
        return 1;
    return 2;
}

const char* GetFxName(int iTier)
{
    switch (iTier)
    {
        case 0:
            return "fire";
        case 1:
            return "fire_med";
        default:
            return "fire_large";
    }
}

float DistanceSquared(const CVector& a, const CVector& b)
{
    const float x = a.fX - b.fX;
    const float y = a.fY - b.fY;
    const float z = a.fZ - b.fZ;
    return x * x + y * y + z * z;
}
}  // namespace

CClientFireManager::CClientFireManager(CClientManager* pManager) : m_pManager(pManager)
{
    if (g_pClientGame && g_pClientGame->GetEvents())
        g_pClientGame->GetEvents()->AddEvent("onClientFireDamage", "victim, damage, responsibleElement", nullptr, false);
}

CClientFireManager::~CClientFireManager()
{
    for (auto& fire : m_Fires)
        DestroyFx(fire);
    m_Fires.clear();
}

bool CClientFireManager::IsFireElement(const CClientEntity* pElement)
{
    return pElement && pElement->GetType() == CCLIENTDUMMY && const_cast<CClientEntity*>(pElement)->GetTypeName() == "fire";
}

void CClientFireManager::Register(CClientDummy* pFire)
{
    if (!pFire || !IsFireElement(pFire))
        return;

    const auto found = std::find_if(m_Fires.begin(), m_Fires.end(), [pFire](const SFireEntry& entry) { return entry.pElement == pFire; });
    if (found != m_Fires.end())
        return;

    SFireEntry entry;
    entry.pElement = pFire;
    m_Fires.push_back(std::move(entry));
}

void CClientFireManager::Unregister(CClientDummy* pFire)
{
    auto found = std::find_if(m_Fires.begin(), m_Fires.end(), [pFire](const SFireEntry& entry) { return entry.pElement == pFire; });
    if (found == m_Fires.end())
        return;

    DestroyFx(*found);
    m_Fires.erase(found);
}

void CClientFireManager::DestroyFx(SFireEntry& entry)
{
    if (entry.pFxSystem)
    {
        g_pGame->GetFxManager()->DestroyFxSystem(entry.pFxSystem);
        entry.pFxSystem = nullptr;
    }
    entry.iFxTier = -1;
}

void CClientFireManager::RecreateFx(SFireEntry& entry, int iTier, const CVector& vecPosition)
{
    DestroyFx(entry);

    entry.pFxSystem = g_pGame->GetFxManager()->CreateFxSystem(GetFxName(iTier), vecPosition, nullptr, 0, true);
    if (entry.pFxSystem)
    {
        entry.pFxSystem->PlayAndKill();
        entry.iFxTier = iTier;
    }
}

void CClientFireManager::TryDamage(SFireEntry& entry, CClientEntity* pVictim, const CVector& vecPosition, float fRadiusSq, unsigned char ucMask, float fDamage)
{
    if (!pVictim || pVictim == entry.pElement || pVictim->IsBeingDeleted() || pVictim->IsOnFire())
        return;

    unsigned char targetBit = 0;
    switch (pVictim->GetType())
    {
        case CCLIENTPLAYER:
            targetBit = DAMAGE_PLAYERS;
            break;
        case CCLIENTPED:
            targetBit = DAMAGE_PEDS;
            break;
        case CCLIENTVEHICLE:
            targetBit = DAMAGE_VEHICLES;
            break;
        case CCLIENTOBJECT:
            targetBit = DAMAGE_OBJECTS;
            break;
        default:
            return;
    }

    if (!(ucMask & targetBit))
        return;

    if (pVictim->GetDimension() != entry.pElement->GetDimension() || pVictim->GetInterior() != entry.pElement->GetInterior())
        return;

    CVector victimPosition;
    pVictim->GetPosition(victimPosition);
    if (DistanceSquared(vecPosition, victimPosition) > fRadiusSq)
        return;

    const unsigned long long now = GetTickCount64_();
    auto& victimTick = entry.LastVictimPulse[pVictim];
    if (victimTick && now - victimTick < DAMAGE_INTERVAL_MS)
        return;
    victimTick = now;

    CClientEntity* pResponsible = GetElement(entry.pElement, KEY_SOURCE);
    CLuaArguments arguments;
    arguments.PushElement(pVictim);
    arguments.PushNumber(fDamage);
    arguments.PushElement(pResponsible);

    // CallEvent returns false when a handler cancels the event. Only ignite the victim
    // after script policy has had a chance to veto the interaction.
    if (!entry.pElement->CallEvent("onClientFireDamage", arguments, true))
        return;

    pVictim->SetOnFire(true);
}

void CClientFireManager::ProcessDamage(SFireEntry& entry, const CVector& vecPosition, float fStrength, unsigned char ucMask)
{
    const float radius = std::max(1.2f, 1.0f + fStrength * 0.65f);
    const float radiusSq = radius * radius;
    const float damage = std::max(1.0f, fStrength * 10.0f);

    if (ucMask & DAMAGE_PLAYERS)
    {
        for (auto iter = m_pManager->GetPlayerManager()->IterBegin(); iter != m_pManager->GetPlayerManager()->IterEnd(); ++iter)
            TryDamage(entry, *iter, vecPosition, radiusSq, ucMask, damage);
    }

    if (ucMask & DAMAGE_PEDS)
    {
        for (auto iter = m_pManager->GetPedManager()->IterBegin(); iter != m_pManager->GetPedManager()->IterEnd(); ++iter)
            TryDamage(entry, *iter, vecPosition, radiusSq, ucMask, damage);
    }

    if (ucMask & DAMAGE_VEHICLES)
    {
        for (auto iter = m_pManager->GetVehicleManager()->IterBegin(); iter != m_pManager->GetVehicleManager()->IterEnd(); ++iter)
            TryDamage(entry, *iter, vecPosition, radiusSq, ucMask, damage);
    }

    if (ucMask & DAMAGE_OBJECTS)
    {
        for (CClientObject* pObject : m_pManager->GetObjectManager()->GetObjects())
            TryDamage(entry, pObject, vecPosition, radiusSq, ucMask, damage);
    }
}

void CClientFireManager::ProcessEntry(SFireEntry& entry, std::vector<CClientDummy*>& expiredLocalFires)
{
    CClientDummy* pFire = entry.pElement;
    if (!pFire || pFire->IsBeingDeleted())
        return;

    double expiry = 0.0;
    GetNumber(pFire, KEY_EXPIRY, expiry);
    if (expiry > 0.0)
    {
        const double now = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        if (now >= expiry)
        {
            DestroyFx(entry);
            if (pFire->IsLocalEntity())
                expiredLocalFires.push_back(pFire);
            return;
        }
    }

    CClientPlayer* pLocalPlayer = m_pManager->GetPlayerManager()->GetLocalPlayer();
    if (!pLocalPlayer || pLocalPlayer->GetDimension() != pFire->GetDimension() || pLocalPlayer->GetInterior() != pFire->GetInterior())
    {
        DestroyFx(entry);
        return;
    }

    CVector position;
    pFire->GetPosition(position);

    CClientEntity* pTarget = GetElement(pFire, KEY_TARGET);
    if (pTarget && !pTarget->IsBeingDeleted() && pTarget->GetDimension() == pFire->GetDimension() && pTarget->GetInterior() == pFire->GetInterior())
        pTarget->GetPosition(position);

    double strengthValue = 1.0;
    GetNumber(pFire, KEY_STRENGTH, strengthValue);
    const float strength = std::max(0.1f, static_cast<float>(strengthValue));
    const int tier = GetFxTier(strength);

    if (!entry.pFxSystem || entry.iFxTier != tier)
        RecreateFx(entry, tier, position);
    else
        entry.pFxSystem->SetPosition(position);

    bool damageEnabled = true;
    GetBool(pFire, KEY_DAMAGE, damageEnabled);
    if (!damageEnabled)
        return;

    double maskValue = DAMAGE_ALL;
    GetNumber(pFire, KEY_DAMAGE_MASK, maskValue);
    const unsigned char mask = static_cast<unsigned char>(static_cast<unsigned int>(maskValue)) & DAMAGE_ALL;
    if (!mask)
        return;

    ProcessDamage(entry, position, strength, mask);
}

void CClientFireManager::DoPulse()
{
    std::vector<CClientDummy*> expiredLocalFires;

    for (auto& fire : m_Fires)
        ProcessEntry(fire, expiredLocalFires);

    for (CClientDummy* pFire : expiredLocalFires)
    {
        if (pFire && !pFire->IsBeingDeleted())
            CStaticFunctionDefinitions::DestroyElement(*pFire);
    }
}
