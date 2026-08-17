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
    static PlantTriangleHandle CreateTriangle(const CVector& v1, const CVector& v2, const CVector& v3, std::uint8_t surface, float density);
    static void                DestroyTriangle(PlantTriangleHandle handle);

    static bool IsValidSurface(std::uint8_t surface);
    static bool IsValidTriangle(const CVector& v1, const CVector& v2, const CVector& v3);
};
