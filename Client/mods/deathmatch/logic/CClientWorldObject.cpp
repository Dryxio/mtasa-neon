/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CClientWorldObject.cpp
 *  PURPOSE:     Client-only proxy for GTA-owned dynamic world objects
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CClientWorldObject.h"
#include "../../../game_sa/CEntitySA.h"
#include "../../../game_sa/CObjectSA.h"

#include <cmath>

namespace
{
    constexpr float MIN_AXIS_LENGTH = 0.0001f;

    void UpdateRw(CEntitySAInterface* pInterface)
    {
        if (!pInterface)
            return;

        using UpdateRwFn = void(__thiscall*)(CEntitySAInterface*);
        reinterpret_cast<UpdateRwFn>(0x446F90)(pInterface);
    }

    float AxisLength(const CVector& axis)
    {
        return std::sqrt(axis.fX * axis.fX + axis.fY * axis.fY + axis.fZ * axis.fZ);
    }

    CVector NormalizedAxis(const CVector& axis)
    {
        const float length = AxisLength(axis);
        return length > MIN_AXIS_LENGTH ? axis / length : axis;
    }
}

CClientWorldObject::CClientWorldObject(CClientManager* pManager, CEntitySAInterface* pDummy, CObjectSAInterface* pObject)
    : CClientDummy(pManager, INVALID_ELEMENT_ID, "worldobject"), m_pDummy(pDummy), m_pObject(pObject)
{
    // GTA owns both interfaces. The MTA element only observes and edits their
    // transform, so scripts must never be able to destroy the native object.
    MakeSystemEntity();
    if (m_pObject)
        m_usModel = m_pObject->m_nModelIndex;
    else if (m_pDummy)
        m_usModel = m_pDummy->m_nModelIndex;
    RefreshFromGame();
}

void CClientWorldObject::Attach(CObjectSAInterface* pObject)
{
    if (!pObject)
        return;

    if (m_pObject == pObject)
    {
        RefreshFromGame();
        return;
    }

    const bool wasStreamedIn = m_pObject != nullptr;
    m_pObject = pObject;
    m_usModel = pObject->m_nModelIndex;
    RefreshFromGame();

    if (!wasStreamedIn)
        FireStreamEvent("onClientElementStreamIn");
}

void CClientWorldObject::Detach()
{
    if (!m_pObject)
        return;

    // ConvertToDummyObject copies the final object matrix back to the persistent
    // dummy before deleting the CObject. Read the dummy after detaching instead
    // of ever touching the now-stale object pointer.
    m_pObject = nullptr;
    RefreshFromGame();
    FireStreamEvent("onClientElementStreamOut");
}

void CClientWorldObject::RefreshFromGame()
{
    CEntitySAInterface* pInterface = GetTransformInterface();
    if (!pInterface)
        return;

    ReadTransform(pInterface, m_vecPosition, m_vecRotation);
    m_usModel = pInterface->m_nModelIndex;
}

bool CClientWorldObject::GetMatrix(CMatrix& matrix) const
{
    CEntitySAInterface* pInterface = GetTransformInterface();
    if (pInterface && pInterface->matrix)
    {
        pInterface->matrix->ConvertToMatrix(matrix);
        return true;
    }

    return CClientEntity::GetMatrix(matrix);
}

bool CClientWorldObject::SetMatrix(const CMatrix& matrix)
{
    CEntitySAInterface* pInterface = GetTransformInterface();
    if (pInterface && pInterface->matrix)
    {
        pInterface->matrix->SetFromMatrixSkipPadding(matrix);
        UpdateRw(pInterface);
        RefreshFromGame();
        return true;
    }

    return CClientEntity::SetMatrix(matrix);
}

void CClientWorldObject::GetPosition(CVector& vecPosition) const
{
    CEntitySAInterface* pInterface = GetTransformInterface();
    if (pInterface)
    {
        CVector rotation;
        ReadTransform(pInterface, vecPosition, rotation);
        return;
    }

    vecPosition = m_vecPosition;
}

void CClientWorldObject::SetPosition(const CVector& vecPosition)
{
    m_vecPosition = vecPosition;

    CEntitySAInterface* pInterface = GetTransformInterface();
    if (!pInterface)
        return;

    if (pInterface->matrix)
        pInterface->matrix->vPos = vecPosition;
    else
        pInterface->m_transform.m_translate = vecPosition;

    UpdateRw(pInterface);
}

void CClientWorldObject::GetRotationRadians(CVector& vecOutRadians) const
{
    CEntitySAInterface* pInterface = GetTransformInterface();
    if (pInterface)
    {
        CVector position;
        ReadTransform(pInterface, position, vecOutRadians);
        return;
    }

    vecOutRadians = m_vecRotation;
}

void CClientWorldObject::SetRotationRadians(const CVector& vecRadians)
{
    m_vecRotation = vecRadians;

    CEntitySAInterface* pInterface = GetTransformInterface();
    if (!pInterface)
        return;

    pInterface->SetOrientation(vecRadians.fX, vecRadians.fY, vecRadians.fZ);
    UpdateRw(pInterface);
}

CEntitySAInterface* CClientWorldObject::GetTransformInterface() const
{
    return m_pObject ? static_cast<CEntitySAInterface*>(m_pObject) : m_pDummy;
}

void CClientWorldObject::ReadTransform(CEntitySAInterface* pInterface, CVector& vecPosition, CVector& vecRotation) const
{
    if (!pInterface)
        return;

    if (!pInterface->matrix)
    {
        vecPosition = pInterface->m_transform.m_translate;
        vecRotation = CVector(0.0f, 0.0f, pInterface->m_transform.m_heading);
        return;
    }

    vecPosition = pInterface->matrix->vPos;

    // Match CClientObject's ZXY decomposition so matrix reads expose the same
    // orientation convention for native and MTA-created objects.
    const CVector right = NormalizedAxis(pInterface->matrix->vRight);
    const CVector front = NormalizedAxis(pInterface->matrix->vFront);
    const CVector up = NormalizedAxis(pInterface->matrix->vUp);
    vecRotation.fX = std::asin(std::clamp(front.fZ, -1.0f, 1.0f));
    vecRotation.fY = std::atan2(-right.fZ, up.fZ);
    vecRotation.fZ = std::atan2(-front.fX, front.fY);
}

void CClientWorldObject::FireStreamEvent(const char* szEvent)
{
    CLuaArguments arguments;
    CallEvent(szEvent, arguments, true);
}
