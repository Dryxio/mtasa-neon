/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientFoliageManager.cpp
 *  PURPOSE:     Client custom foliage manager
 *
 *****************************************************************************/

#include <StdInc.h>
#include "CClientFoliageManager.h"
#include "CClientFoliage.h"

CClientFoliageManager& CClientFoliageManager::GetSingleton()
{
    static CClientFoliageManager manager;
    return manager;
}

CClientFoliage* CClientFoliageManager::Create(CClientManager* pManager, ElementID ID)
{
    if (m_List.size() >= MAX_CUSTOM_FOLIAGE)
        return nullptr;

    if (ID != INVALID_ELEMENT_ID && Get(ID))
        return nullptr;

    return new CClientFoliage(pManager, ID);
}

CClientFoliage* CClientFoliageManager::Get(ElementID ID)
{
    for (CClientFoliage* pFoliage : m_List)
    {
        if (pFoliage->GetID() == ID)
            return pFoliage;
    }

    return nullptr;
}

void CClientFoliageManager::SetDimension(unsigned short usDimension)
{
    m_usDimension = usDimension;

    for (CClientFoliage* pFoliage : m_List)
        pFoliage->RelateDimension(usDimension);
}

void CClientFoliageManager::AddToList(CClientFoliage* pFoliage)
{
    m_List.push_back(pFoliage);
}

void CClientFoliageManager::RemoveFromList(CClientFoliage* pFoliage)
{
    m_List.remove(pFoliage);
}
