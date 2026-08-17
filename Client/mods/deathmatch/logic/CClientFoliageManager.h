/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientFoliageManager.h
 *  PURPOSE:     Client custom foliage manager
 *
 *****************************************************************************/

#pragma once

#include "CClientEntity.h"
#include <cstddef>
#include <list>

class CClientFoliage;
class CClientManager;

class CClientFoliageManager
{
    friend class CClientFoliage;

public:
    static CClientFoliageManager& GetSingleton();

    CClientFoliage* Create(CClientManager* pManager, ElementID ID);
    CClientFoliage* Get(ElementID ID);

    void           SetDimension(unsigned short usDimension);
    unsigned short GetDimension() const { return m_usDimension; }
    std::size_t    GetCount() const { return m_List.size(); }

    static constexpr std::size_t MAX_CUSTOM_FOLIAGE = 64;

private:
    CClientFoliageManager() = default;

    void AddToList(CClientFoliage* pFoliage);
    void RemoveFromList(CClientFoliage* pFoliage);

    std::list<CClientFoliage*> m_List;
    unsigned short             m_usDimension = 0;
};
