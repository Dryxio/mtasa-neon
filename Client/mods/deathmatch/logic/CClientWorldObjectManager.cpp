/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CClientWorldObjectManager.cpp
 *  PURPOSE:     Discovery and event bridge for GTA-owned dynamic world objects
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CClientWorldObjectManager.h"
#include "CClientWorldObject.h"
#include "../../../game_sa/CPoolSAInterface.h"
#include "../../../game_sa/CPoolsSA.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using WorldObjectMap = std::unordered_map<CEntitySAInterface*, std::unique_ptr<CClientWorldObject>>;
    using LiveObjectMap = std::unordered_map<CObjectSAInterface*, CClientWorldObject*>;

    WorldObjectMap                                  g_worldObjects;
    LiveObjectMap                                   g_liveObjects;
    std::unordered_set<CEntitySAInterface*>         g_streamedDummies;
    CClientGame*                                    g_pRegisteredGame = nullptr;
    bool                                            g_bHandlersInstalled = false;

    CPoolSAInterface<CObjectSAInterface>* GetNativeObjectPool()
    {
        auto** ppPool = reinterpret_cast<CPoolSAInterface<CObjectSAInterface>**>(CLASS_CObjectPool);
        return ppPool ? *ppPool : nullptr;
    }

    CPoolSAInterface<CEntitySAInterface>* GetNativeDummyPool()
    {
        auto** ppPool = reinterpret_cast<CPoolSAInterface<CEntitySAInterface>**>(CLASS_CDummyPool);
        return ppPool ? *ppPool : nullptr;
    }

    bool IsDummyAlive(CPoolSAInterface<CEntitySAInterface>* pPool, CEntitySAInterface* pDummy)
    {
        if (!pPool || !pDummy || !pPool->m_pObjects || !pPool->m_byteMap)
            return false;

        const std::int32_t index = pPool->GetObjectIndexSafe(pDummy);
        return index >= 0 && !pPool->IsEmpty(index) && pPool->GetObject(index) == pDummy;
    }

    bool IsNativeWorldObject(CObjectSAInterface* pObject)
    {
        if (!pObject || pObject->nType != ENTITY_TYPE_OBJECT || !pObject->pLinkedObjectDummy)
            return false;

        // CObject::Init writes OBJECT_GAME (1) at offset 316. MTA-created
        // CObjectSA instances overwrite the same byte with OBJECT_MISSION2 (6),
        // so this excludes MTA objects and temporary/flying components.
        return pObject->pad1 == 1;
    }

    bool DefaultObjectBreakHandler(CObjectSAInterface* pObjectInterface, CEntitySAInterface* pAttackerInterface)
    {
        if (!pObjectInterface || !g_pGame)
            return true;

        CPools*                   pPools = g_pGame->GetPools();
        SClientEntity<CObjectSA>* pObjectEntity = pPools ? pPools->GetObject(reinterpret_cast<DWORD*>(pObjectInterface)) : nullptr;
        if (!pObjectEntity)
            return true;

        CClientObject* pClientObject = reinterpret_cast<CClientObject*>(pObjectEntity->pClientEntity);
        if (!pClientObject)
            return true;
        if (!pClientObject->IsBreakable(false))
            return false;

        CClientEntity* pClientAttacker = pPools->GetClientEntity(reinterpret_cast<DWORD*>(pAttackerInterface));
        CClientPed*    pPresentationPed = DynamicCast<CClientPed>(pClientAttacker);
        if (pPresentationPed && pPresentationPed->IsNativeTaskWeaponPresentationActive())
            return false;

        pClientObject->SetHealth(0.0f);

        CLuaArguments arguments;
        if (pClientAttacker)
            arguments.PushElement(pClientAttacker);
        else
            arguments.PushNil();
        return pClientObject->CallEvent("onClientObjectBreak", arguments, true);
    }

    CClientWorldObject* AdoptObject(CObjectSAInterface* pObject)
    {
        if (!IsNativeWorldObject(pObject) || !g_pClientGame)
            return nullptr;

        CEntitySAInterface* pDummy = pObject->pLinkedObjectDummy;
        auto                iter = g_worldObjects.find(pDummy);
        if (iter == g_worldObjects.end())
        {
            auto proxy = std::make_unique<CClientWorldObject>(g_pClientGame->GetManager(), pDummy, pObject);
            proxy->SetParent(g_pClientGame->GetRootEntity());
            CClientWorldObject* pProxy = proxy.get();
            g_worldObjects.emplace(pDummy, std::move(proxy));
            g_liveObjects[pObject] = pProxy;
            return pProxy;
        }

        iter->second->Attach(pObject);
        g_liveObjects[pObject] = iter->second.get();
        return iter->second.get();
    }

    CClientWorldObject* FindOrAdoptObject(CObjectSAInterface* pObject)
    {
        const auto iter = g_liveObjects.find(pObject);
        if (iter != g_liveObjects.end())
        {
            // GTA may recycle an object-pool address between scans. Only reuse
            // the cached proxy when the persistent dummy identity still matches.
            if (iter->second && iter->second->GetDummyInterface() == pObject->pLinkedObjectDummy)
                return iter->second;
            g_liveObjects.erase(iter);
        }
        return AdoptObject(pObject);
    }

    void RegisterForGame(CClientGame* pGame)
    {
        if (!pGame || !g_pMultiplayer)
            return;

        if (g_pRegisteredGame == pGame && g_bHandlersInstalled)
            return;

        g_pRegisteredGame = pGame;
        pGame->GetEvents()->AddEvent("onClientWorldObjectDamage", "loss, attacker, model, x, y, z", nullptr, false);
        pGame->GetEvents()->AddEvent("onClientWorldObjectBreak", "attacker, model, x, y, z", nullptr, false);
        g_pMultiplayer->SetObjectDamageHandler(CClientWorldObjectManager::ObjectDamageHandler);
        g_pMultiplayer->SetObjectBreakHandler(CClientWorldObjectManager::ObjectBreakHandler);
        g_bHandlersInstalled = true;
    }

    void ClearProxies()
    {
        g_liveObjects.clear();
        g_streamedDummies.clear();
        g_worldObjects.clear();
    }
}

void CClientWorldObjectManager::Pulse()
{
    if (!g_pClientGame || !g_pGame || !g_pMultiplayer)
        return;

    RegisterForGame(g_pClientGame);

    if (g_pGame->GetSystemState() != SystemState::GS_PLAYING_GAME)
    {
        ClearProxies();
        return;
    }

    auto* pObjectPool = GetNativeObjectPool();
    auto* pDummyPool = GetNativeDummyPool();
    if (!pObjectPool || !pDummyPool || !pObjectPool->m_pObjects || !pObjectPool->m_byteMap || !pDummyPool->m_pObjects || !pDummyPool->m_byteMap)
        return;

    g_liveObjects.clear();
    g_streamedDummies.clear();
    if (g_streamedDummies.bucket_count() < 256)
        g_streamedDummies.reserve(256);

    for (int i = 0; i < pObjectPool->m_nSize; ++i)
    {
        if (pObjectPool->IsEmpty(i))
            continue;

        CObjectSAInterface* pObject = pObjectPool->GetObject(i);
        if (!IsNativeWorldObject(pObject) || !IsDummyAlive(pDummyPool, pObject->pLinkedObjectDummy))
            continue;

        CClientWorldObject* pProxy = AdoptObject(pObject);
        if (!pProxy)
            continue;

        pProxy->RefreshFromGame();
        g_streamedDummies.insert(pObject->pLinkedObjectDummy);
    }

    for (auto iter = g_worldObjects.begin(); iter != g_worldObjects.end();)
    {
        if (!IsDummyAlive(pDummyPool, iter->first))
        {
            iter = g_worldObjects.erase(iter);
            continue;
        }

        if (!g_streamedDummies.contains(iter->first))
            iter->second->Detach();
        ++iter;
    }
}

void CClientWorldObjectManager::Shutdown()
{
    ClearProxies();

    if (g_bHandlersInstalled && g_pMultiplayer)
    {
        g_pMultiplayer->SetObjectDamageHandler(CClientGame::StaticObjectDamageHandler);
        g_pMultiplayer->SetObjectBreakHandler(DefaultObjectBreakHandler);
    }

    g_bHandlersInstalled = false;
    g_pRegisteredGame = nullptr;
}

bool CClientWorldObjectManager::ObjectDamageHandler(CObjectSAInterface* pObjectInterface, float fLoss, CEntitySAInterface* pAttackerInterface)
{
    CClientWorldObject* pProxy = FindOrAdoptObject(pObjectInterface);
    if (!pProxy)
        return CClientGame::StaticObjectDamageHandler(pObjectInterface, fLoss, pAttackerInterface);

    pProxy->RefreshFromGame();
    CVector position;
    pProxy->GetPosition(position);

    CLuaArguments arguments;
    arguments.PushNumber(fLoss);

    CClientEntity* pAttacker = g_pGame->GetPools()->GetClientEntity(reinterpret_cast<DWORD*>(pAttackerInterface));
    if (pAttacker)
        arguments.PushElement(pAttacker);
    else
        arguments.PushNil();

    arguments.PushNumber(pProxy->GetWorldModel());
    arguments.PushNumber(position.fX);
    arguments.PushNumber(position.fY);
    arguments.PushNumber(position.fZ);
    return pProxy->CallEvent("onClientWorldObjectDamage", arguments, true);
}

bool CClientWorldObjectManager::ObjectBreakHandler(CObjectSAInterface* pObjectInterface, CEntitySAInterface* pAttackerInterface)
{
    CClientWorldObject* pProxy = FindOrAdoptObject(pObjectInterface);
    if (!pProxy)
        return DefaultObjectBreakHandler(pObjectInterface, pAttackerInterface);

    pProxy->RefreshFromGame();
    CVector position;
    pProxy->GetPosition(position);

    CLuaArguments arguments;
    CClientEntity* pAttacker = g_pGame->GetPools()->GetClientEntity(reinterpret_cast<DWORD*>(pAttackerInterface));
    if (pAttacker)
        arguments.PushElement(pAttacker);
    else
        arguments.PushNil();

    arguments.PushNumber(pProxy->GetWorldModel());
    arguments.PushNumber(position.fX);
    arguments.PushNumber(position.fY);
    arguments.PushNumber(position.fZ);
    return pProxy->CallEvent("onClientWorldObjectBreak", arguments, true);
}
