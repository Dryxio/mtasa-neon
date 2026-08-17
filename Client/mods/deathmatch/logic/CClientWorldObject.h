/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CClientWorldObject.h
 *  PURPOSE:     Client-only proxy for GTA-owned dynamic world objects
 *
 *****************************************************************************/

#pragma once

#include "CClientDummy.h"

class CEntitySAInterface;
class CObjectSAInterface;

class CClientWorldObject final : public CClientDummy
{
public:
    CClientWorldObject(CClientManager* pManager, CEntitySAInterface* pDummy, CObjectSAInterface* pObject);
    ~CClientWorldObject() override = default;

    void Attach(CObjectSAInterface* pObject);
    void Detach();
    void RefreshFromGame();

    CObjectSAInterface* GetGameObject() const { return m_pObject; }
    CEntitySAInterface* GetDummyInterface() const { return m_pDummy; }
    unsigned short      GetWorldModel() const { return m_usModel; }
    bool                IsWorldObjectStreamedIn() const { return m_pObject != nullptr; }

    bool GetMatrix(CMatrix& matrix) const override;
    bool SetMatrix(const CMatrix& matrix) override;
    void GetPosition(CVector& vecPosition) const override;
    void SetPosition(const CVector& vecPosition) override;
    void GetRotationRadians(CVector& vecOutRadians) const override;
    void SetRotationRadians(const CVector& vecRadians) override;

private:
    CEntitySAInterface* GetTransformInterface() const;
    void                ReadTransform(CEntitySAInterface* pInterface, CVector& vecPosition, CVector& vecRotation) const;
    void                FireStreamEvent(const char* szEvent);

private:
    CEntitySAInterface* m_pDummy = nullptr;
    CObjectSAInterface* m_pObject = nullptr;
    unsigned short      m_usModel = 0;
    CVector             m_vecPosition;
    CVector             m_vecRotation;
};
