/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientFoliage.h
 *  PURPOSE:     Client custom foliage element
 *
 *****************************************************************************/

#pragma once

#include "CClientEntity.h"
#include <game/CPlantManager.h>
#include <cstdint>

class CClientFoliageManager;

class CClientFoliage final : public CClientEntity
{
    friend class CClientFoliageManager;

public:
    CClientFoliage(CClientManager* pManager, ElementID ID);
    ~CClientFoliage();

    eClientEntityType GetType() const { return CCLIENTUNKNOWN; }

    bool Initialize(const CVector& v1, const CVector& v2, const CVector& v3, std::uint8_t surface, float density);

    void GetPosition(CVector& vecPosition) const override;
    void SetPosition(const CVector& vecPosition) override;
    void SetDimension(unsigned short usDimension) override;

    void GetVertices(CVector& v1, CVector& v2, CVector& v3) const;
    bool SetVertices(const CVector& v1, const CVector& v2, const CVector& v3);

    std::uint8_t GetSurface() const { return m_ucSurface; }
    bool         SetSurface(std::uint8_t surface);

    float GetDensity() const { return m_fDensity; }
    bool  SetDensity(float density);

    bool StreamIn();
    void StreamOut();
    void RelateDimension(unsigned short usDimension);
    void Unlink();

private:
    bool Rebuild(const CVector& v1, const CVector& v2, const CVector& v3, std::uint8_t surface, float density);

    CClientFoliageManager* m_pFoliageManager = nullptr;
    CVector                m_vecVertices[3];
    std::uint8_t           m_ucSurface = 0;
    float                  m_fDensity = 1.0f;
    PlantTriangleHandle    m_pNativeTriangle = nullptr;
};
