/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientBirdManager.h
 *  PURPOSE:     Managed bird simulation, rendering and gunshot policy
 *
 *****************************************************************************/

#pragma once

#include "CClientEntity.h"
#include <cstddef>
#include <list>

class CClientBird;
class CClientManager;

class CClientBirdManager
{
    friend class CClientBird;

public:
    static CClientBirdManager& GetSingleton();

    CClientBird* Create(CClientManager* pManager, ElementID ID);
    CClientBird* Get(ElementID ID);

    void DoPulse(CClientManager* pManager);
    void DoRender(CClientManager* pManager);
    bool HandleGunShot(const CVector& start, const CVector& end, CClientEntity* pAttacker, int weapon);

    std::size_t GetCount() const { return m_List.size(); }

    static constexpr std::size_t MAX_MANAGED_BIRDS = 256;

private:
    CClientBirdManager();

    void AddToList(CClientBird* pBird);
    void RemoveFromList(CClientBird* pBird);

    std::list<CClientBird*> m_List;
    unsigned long long      m_ullLastPulse = 0;
};
