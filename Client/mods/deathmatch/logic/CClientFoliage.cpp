/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientFoliage.cpp
 *  PURPOSE:     Client custom foliage element
 *
 *****************************************************************************/

#include <StdInc.h>
#include "CClientFoliage.h"
#include "CClientFoliageManager.h"

CClientFoliage::CClientFoliage(CClientManager* pManager, ElementID ID) : CClientEntity(ID)
{
    m_pManager = pManager;
    m_pFoliageManager = &CClientFoliageManager::GetSingleton();

    SetTypeName("foliage");
    m_pFoliageManager->AddToList(this);
}

CClientFoliage::~CClientFoliage()
{
    StreamOut();
    Unlink();
}

bool CClientFoliage::Initialize(const CVector& v1, const CVector& v2, const CVector& v3, std::uint8_t surface, float density)
{
    CPlantManager* plantManager = g_pGame->GetPlantManager();
    if (!plantManager || !plantManager->IsValidTriangle(v1, v2, v3) || !plantManager->IsValidSurface(surface) || !std::isfinite(density) || density < 0.0f || density > 10.0f)
        return false;

    m_vecVertices[0] = v1;
    m_vecVertices[1] = v2;
    m_vecVertices[2] = v3;
    m_ucSurface = surface;
    m_fDensity = density;

    if (m_usDimension == m_pFoliageManager->GetDimension())
        return StreamIn();

    return true;
}

void CClientFoliage::GetPosition(CVector& vecPosition) const
{
    vecPosition.fX = (m_vecVertices[0].fX + m_vecVertices[1].fX + m_vecVertices[2].fX) / 3.0f;
    vecPosition.fY = (m_vecVertices[0].fY + m_vecVertices[1].fY + m_vecVertices[2].fY) / 3.0f;
    vecPosition.fZ = (m_vecVertices[0].fZ + m_vecVertices[1].fZ + m_vecVertices[2].fZ) / 3.0f;
}

void CClientFoliage::SetPosition(const CVector& vecPosition)
{
    CVector currentPosition;
    GetPosition(currentPosition);

    const CVector delta(vecPosition.fX - currentPosition.fX, vecPosition.fY - currentPosition.fY, vecPosition.fZ - currentPosition.fZ);
    SetVertices(CVector(m_vecVertices[0].fX + delta.fX, m_vecVertices[0].fY + delta.fY, m_vecVertices[0].fZ + delta.fZ),
                CVector(m_vecVertices[1].fX + delta.fX, m_vecVertices[1].fY + delta.fY, m_vecVertices[1].fZ + delta.fZ),
                CVector(m_vecVertices[2].fX + delta.fX, m_vecVertices[2].fY + delta.fY, m_vecVertices[2].fZ + delta.fZ));
}

void CClientFoliage::SetDimension(unsigned short usDimension)
{
    CClientEntity::SetDimension(usDimension);
    RelateDimension(m_pFoliageManager->GetDimension());
}

void CClientFoliage::GetVertices(CVector& v1, CVector& v2, CVector& v3) const
{
    v1 = m_vecVertices[0];
    v2 = m_vecVertices[1];
    v3 = m_vecVertices[2];
}

bool CClientFoliage::SetVertices(const CVector& v1, const CVector& v2, const CVector& v3)
{
    return Rebuild(v1, v2, v3, m_ucSurface, m_fDensity);
}

bool CClientFoliage::SetSurface(std::uint8_t surface)
{
    return Rebuild(m_vecVertices[0], m_vecVertices[1], m_vecVertices[2], surface, m_fDensity);
}

bool CClientFoliage::SetDensity(float density)
{
    return Rebuild(m_vecVertices[0], m_vecVertices[1], m_vecVertices[2], m_ucSurface, density);
}

bool CClientFoliage::StreamIn()
{
    if (m_pNativeTriangle)
        return true;

    if (m_usDimension != m_pFoliageManager->GetDimension())
        return true;

    CPlantManager* plantManager = g_pGame->GetPlantManager();
    m_pNativeTriangle = plantManager ? plantManager->CreateTriangle(m_vecVertices[0], m_vecVertices[1], m_vecVertices[2], m_ucSurface, m_fDensity) : nullptr;
    return m_pNativeTriangle != nullptr;
}

void CClientFoliage::StreamOut()
{
    if (!m_pNativeTriangle)
        return;

    if (CPlantManager* plantManager = g_pGame->GetPlantManager())
        plantManager->DestroyTriangle(m_pNativeTriangle);
    m_pNativeTriangle = nullptr;
}

void CClientFoliage::RelateDimension(unsigned short usDimension)
{
    if (usDimension == m_usDimension)
        StreamIn();
    else
        StreamOut();
}

void CClientFoliage::Unlink()
{
    if (m_pFoliageManager)
    {
        m_pFoliageManager->RemoveFromList(this);
        m_pFoliageManager = nullptr;
    }
}

bool CClientFoliage::Rebuild(const CVector& v1, const CVector& v2, const CVector& v3, std::uint8_t surface, float density)
{
    CPlantManager* plantManager = g_pGame->GetPlantManager();
    if (!plantManager || !plantManager->IsValidTriangle(v1, v2, v3) || !plantManager->IsValidSurface(surface) || !std::isfinite(density) || density < 0.0f || density > 10.0f)
        return false;

    const CVector oldVertices[3] = {m_vecVertices[0], m_vecVertices[1], m_vecVertices[2]};
    const auto    oldSurface = m_ucSurface;
    const float   oldDensity = m_fDensity;
    const bool    active = m_usDimension == m_pFoliageManager->GetDimension();

    if (active)
        StreamOut();

    m_vecVertices[0] = v1;
    m_vecVertices[1] = v2;
    m_vecVertices[2] = v3;
    m_ucSurface = surface;
    m_fDensity = density;

    if (!active || StreamIn())
        return true;

    m_vecVertices[0] = oldVertices[0];
    m_vecVertices[1] = oldVertices[1];
    m_vecVertices[2] = oldVertices[2];
    m_ucSurface = oldSurface;
    m_fDensity = oldDensity;
    StreamIn();
    return false;
}
