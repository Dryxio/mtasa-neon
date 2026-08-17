/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/sdk/game/CPlantManager.h
 *  PURPOSE:     Custom procedural foliage triangle interface
 *
 *****************************************************************************/

#pragma once

#include <CVector.h>
#include <cstdint>

using PlantTriangleHandle = void*;

class CPlantManager
{
public:
    virtual ~CPlantManager() = default;

    virtual PlantTriangleHandle CreateTriangle(const CVector& v1, const CVector& v2, const CVector& v3, std::uint8_t surface, float density) = 0;
    virtual void                DestroyTriangle(PlantTriangleHandle handle) = 0;

    virtual bool IsValidSurface(std::uint8_t surface) = 0;
    virtual bool IsValidTriangle(const CVector& v1, const CVector& v2, const CVector& v3) = 0;
};
