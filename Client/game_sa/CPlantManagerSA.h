#pragma once

#include "CEntitySA.h"
#include <game/CPlantManager.h>

class CPlantManagerSA : public CPlantManager
{
public:
    CPlantManagerSA() = default;
    ~CPlantManagerSA() = default;

    PlantTriangleHandle CreateTriangle(const CVector& v1, const CVector& v2, const CVector& v3, std::uint8_t surface, float density) override;
    void                DestroyTriangle(PlantTriangleHandle handle) override;
    bool                IsValidSurface(std::uint8_t surface) override;
    bool                IsValidTriangle(const CVector& v1, const CVector& v2, const CVector& v3) override;

    void RemovePlant(CEntitySAInterface* enity)
    {
        using CPlantColEntry_Remove = CEntitySAInterface* (*)(CEntitySAInterface*);
        ((CPlantColEntry_Remove)0x5DBEF0)(enity);
    };

    void RemoveAllPlants();
};
