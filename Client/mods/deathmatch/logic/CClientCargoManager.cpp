#include "StdInc.h"
#include "CClientCargoManager.h"
#include <game/CAnimManager.h>
#include <game/CAnimBlock.h>
#include <game/CTasks.h>
#include <cmath>

namespace
{
    bool CanExecute(CClientPed* ped)
    {
        // Remote players must never receive local task submissions, even if a
        // generic sync flag happens to be set during an ownership transition.
        return ped && !ped->IsBeingDeleted() && ped->IsStreamedIn() && ped->GetGamePlayer() && !ped->IsDead() &&
               (ped->IsLocalPlayer() || (ped->GetType() == CCLIENTPED && (ped->IsLocalEntity() || ped->IsSyncing()))) && !ped->GetRealOccupiedVehicle() &&
               !ped->IsGettingIntoVehicle() && !ped->GetAttachedTo() && !ped->IsInWater();
    }

    const char* StateName(int state)
    {
        return state == 1 ? "holding" : state == 2 ? "putting_down" : state == 3 ? "starting" : "released";
    }

    void Notify(CClientPed* ped, CClientObject* object, const char* state, const char* reason)
    {
        CLuaArguments arguments;
        arguments.PushElement(object);
        arguments.PushString(state);
        arguments.PushString(reason);
        ped->CallEvent("onClientPedCarryStateChange", arguments, true);
    }
}

CClientCargoManager& CClientCargoManager::GetSingleton()
{
    static CClientCargoManager manager;
    return manager;
}

CClientCargoManager::Entry* CClientCargoManager::Find(CClientPed* ped)
{
    for (auto& entry : m_entries)
        if (entry.ped == ped)
            return &entry;
    return nullptr;
}

CClientObject* CClientCargoManager::GetObject(CClientPed* ped) const
{
    for (const auto& entry : m_entries)
        if (entry.ped == ped)
            return entry.object;
    return nullptr;
}

const char* CClientCargoManager::GetState(CClientPed* ped) const
{
    for (const auto& entry : m_entries)
        if (entry.ped == ped)
            return StateName(entry.state);
    return "released";
}

bool CClientCargoManager::Start(CLuaMain* owner, CClientPed* ped, CClientObject* object)
{
    if (!owner || !CanExecute(ped) || !ped->IsOnGround() || Find(ped) || !object || object->IsBeingDeleted() || !object->IsStreamedIn() ||
        !object->GetGameObject() || object->GetAttachedTo() || object->GetModel() != 1271 || ped->GetDimension() != object->GetDimension() ||
        ped->GetInterior() != object->GetInterior())
        return false;
    CVector scale;
    object->GetScale(scale);
    if (scale != CVector(1.0f, 1.0f, 1.0f))
        return false;
    for (const auto& entry : m_entries)
        if (entry.object == object)
            return false;
    CVector pedPosition, objectPosition;
    ped->GetPosition(pedPosition);
    object->GetPosition(objectPosition);
    const float distance = (pedPosition - objectPosition).Length();
    if (!std::isfinite(distance) || distance > 3.0f)
        return false;

    auto block = g_pGame->GetAnimManager()->GetAnimationBlock("carry");
    if (!block)
        return false;
    block->Request(BLOCKING, true);
    if (!block->IsLoaded())
        return false;

    const bool frozen = object->IsFrozen();
    object->SetFrozen(false);
    if (!g_pGame->GetTasks()->StartPedCarryObject(ped->GetGamePlayer(), object->GetGameObject()))
    {
        object->SetFrozen(frozen);
        return false;
    }
    Entry entry{};
    entry.recoveryPosition = objectPosition;
    entry.owner = owner;
    entry.ped = ped;
    entry.object = object;
    entry.health = ped->GetHealth();
    entry.started = SharedUtil::GetTickCount64_();
    entry.frozen = frozen;
    m_entries.push_back(entry);
    // Notify only observed native transitions in DoPulse. No synchronous Lua
    // callback may invalidate a successful start before its caller returns.
    return true;
}

bool CClientCargoManager::PutDown(CLuaMain* owner, CClientPed* ped)
{
    Entry* entry = Find(ped);
    if (!entry || entry->owner != owner || entry->putDownRequested || !CanExecute(ped) || !g_pGame->GetTasks()->PutDownPedObject(ped->GetGamePlayer()))
        return false;
    entry->putDownRequested = SharedUtil::GetTickCount64_();
    return true;
}

bool CClientCargoManager::Cancel(CLuaMain* owner, CClientPed* ped)
{
    Entry* entry = Find(ped);
    if (!entry || entry->owner != owner)
        return false;
    Release(ped, "cancelled", true);
    return true;
}

void CClientCargoManager::Release(CClientPed* ped, const char* reason, bool notify)
{
    auto iter = std::find_if(m_entries.begin(), m_entries.end(), [ped](const Entry& entry) { return entry.ped == ped; });
    if (iter == m_entries.end())
        return;
    const Entry entry = *iter;
    m_entries.erase(iter);
    // Cache GTA's final transform before restoring frozen/streaming state;
    // otherwise MTA can snap a dropped box back to its original pickup point.
    if (auto* object = entry.object->GetGameObject())
    {
        CMatrix matrix;
        object->GetMatrix(&matrix);
        CVector rotation;
        entry.object->GetRotationRadians(rotation);
        // A forced interruption in water or mid-fall recovers to the last
        // reachable pose. A native release keeps GTA's observed drop position.
        const bool nativeRelease = strcmp(reason, "put_down") == 0 || strcmp(reason, "native_release") == 0;
        entry.object->SetPosition(nativeRelease ? matrix.vPos : entry.recoveryPosition);
        entry.object->SetRotationRadians(rotation);
        entry.object->SetMoveSpeed(CVector());
        entry.object->SetTurnSpeed(CVector());
    }
    if (ped->GetGamePlayer())
        g_pGame->GetTasks()->CancelPedCarryObject(ped->GetGamePlayer());
    entry.object->SetFrozen(entry.frozen);
    if (notify)
        Notify(ped, entry.object, "released", reason);
}

void CClientCargoManager::OnEntityDestroy(CClientEntity* entity)
{
    // These hooks run BEFORE native model teardown, including model changes
    // and stream-out. GTA entity references alone cannot preserve MTA caches.
    for (size_t i = 0; i < m_entries.size();)
    {
        if (m_entries[i].ped == entity || m_entries[i].object == entity)
            Release(m_entries[i].ped, "entity_unavailable", false);
        else
            ++i;
    }
}

void CClientCargoManager::OnLuaMainDestroy(CLuaMain* owner)
{
    for (size_t i = 0; i < m_entries.size();)
    {
        if (m_entries[i].owner == owner)
            Release(m_entries[i].ped, "resource_stop", false);
        else
            ++i;
    }
}

void CClientCargoManager::DoPulse()
{
    std::vector<CClientPed*> peds;
    for (const auto& entry : m_entries)
        peds.push_back(entry.ped);
    for (auto* ped : peds)
    {
        Entry* entry = Find(ped);
        if (!entry)
            continue;
        const auto now = SharedUtil::GetTickCount64_();
        if (!CanExecute(ped) || entry->object->GetAttachedTo() || ped->GetDimension() != entry->object->GetDimension() ||
            ped->GetInterior() != entry->object->GetInterior())
        {
            Release(ped, "interrupted", true);
            continue;
        }
        if (ped->GetHealth() < entry->health)
        {
            Release(ped, "damage", true);
            continue;
        }
        entry->health = ped->GetHealth();
        if (ped->IsOnGround())
        {
            entry->airborneSince = 0;
            entry->object->GetPosition(entry->recoveryPosition);
        }
        else if (!entry->airborneSince)
            entry->airborneSince = now;
        if (entry->airborneSince && now - entry->airborneSince > 500)
        {
            Release(ped, "fall", true);
            continue;
        }
        const int state = g_pGame->GetTasks()->GetPedCarryState(ped->GetGamePlayer());
        if (!state)
        {
            Release(ped, entry->state == 2 ? "put_down" : "native_release", true);
            continue;
        }
        if ((state == 3 && now - entry->started > 5000) || (entry->putDownRequested && now - entry->putDownRequested > 8000))
        {
            Release(ped, "timeout", true);
            continue;
        }
        if (state == 2 && !entry->putDownRequested)
            entry->putDownRequested = now;  // Includes GTA's exit-vehicle input.
        if (state != entry->state)
        {
            entry->state = state;
            Notify(ped, entry->object, StateName(state), "native");
            // Event handlers may stop the resource or destroy either element.
        }
    }
}
