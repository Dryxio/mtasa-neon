/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CClientDummy.cpp
 *  PURPOSE:     Dummy entity class
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CClientFireManager.h"

CClientDummy::CClientDummy(CClientManager* pManager, ElementID ID, const char* szTypeName) : ClassInit(this), CClientEntity(ID)
{
    SetTypeName(szTypeName);

    m_pManager = pManager;
    if (pManager)
    {
        m_pGroups = pManager->GetGroups();

        if (m_pGroups)
        {
            m_pGroups->AddToList(this);
        }

        if (pManager->GetFireManager() && GetTypeName() == "fire")
            pManager->GetFireManager()->Register(this);
    }
    else
    {
        m_pGroups = NULL;
    }
}

CClientDummy::~CClientDummy()
{
    Unlink();
}

void CClientDummy::Unlink()
{
    if (m_pManager && m_pManager->GetFireManager() && GetTypeName() == "fire")
        m_pManager->GetFireManager()->Unregister(this);

    if (m_pGroups)
    {
        m_pGroups->RemoveFromList(this);
        m_pGroups = NULL;
    }
}
