#include "StdInc.h"
#include "CPlantManagerSA.h"
#include "CPtrNodeSingleListSA.h"
#include <game/CPlantManager.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

class CPlantLocTri;

class CPlantColEntEntry
{
public:
    CEntitySAInterface* m_Entity;
    CPlantLocTri**      m_Objects;
    uint16              m_numTriangles;
    CPlantColEntEntry*  m_NextEntry;
    CPlantColEntEntry*  m_PrevEntry;

public:
    void ReleaseEntry()
    {
        using CPlantColEntEntry_ReleaseEntry = void*(__thiscall*)(CPlantColEntEntry*);
        ((CPlantColEntEntry_ReleaseEntry)0x5DB8A0)(this);
    };
};

namespace
{
    constexpr std::uintptr_t FUNC_PLANT_LOC_TRI_ADD = 0x5DC290;
    constexpr std::uintptr_t FUNC_PLANT_LOC_TRI_RELEASE = 0x5DB6D0;
    constexpr std::uintptr_t FUNC_GET_SURF_PROPERTIES = 0x6F9DE0;
    constexpr std::uintptr_t VAR_UNUSED_LOC_TRI_LIST_HEAD = 0xC03984;
    constexpr float          MAX_CUSTOM_DENSITY = 10.0f;

    struct CPlantLocTriSAInterface
    {
        CVector m_V1;
        CVector m_V2;
        CVector m_V3;
        CVector m_Center;
        float   m_fSphereRadius;
        float   m_fSeed[3];
        uint16  m_usMaxPlants[3];
        uint8   m_ucSurface;
        uint8   m_ucLighting;
        uint8   m_ucFlags;
        CPlantLocTriSAInterface* m_pNext;
        CPlantLocTriSAInterface* m_pPrev;
    };

    static_assert(sizeof(CPlantLocTriSAInterface) == 0x54, "Unexpected CPlantLocTri layout");

    bool IsFiniteVector(const CVector& value)
    {
        return std::isfinite(value.fX) && std::isfinite(value.fY) && std::isfinite(value.fZ);
    }
}

bool CPlantManagerSA::IsValidSurface(std::uint8_t surface)
{
    using GetSurfProperties = void*(__cdecl*)(uint16);
    return reinterpret_cast<GetSurfProperties>(FUNC_GET_SURF_PROPERTIES)(surface) != nullptr;
}

bool CPlantManagerSA::IsValidTriangle(const CVector& v1, const CVector& v2, const CVector& v3)
{
    if (!IsFiniteVector(v1) || !IsFiniteVector(v2) || !IsFiniteVector(v3))
        return false;

    const float ax = v2.fX - v1.fX;
    const float ay = v2.fY - v1.fY;
    const float az = v2.fZ - v1.fZ;
    const float bx = v3.fX - v1.fX;
    const float by = v3.fY - v1.fY;
    const float bz = v3.fZ - v1.fZ;

    const float cx = ay * bz - az * by;
    const float cy = az * bx - ax * bz;
    const float cz = ax * by - ay * bx;
    const float crossLengthSquared = cx * cx + cy * cy + cz * cz;

    return std::isfinite(crossLengthSquared) && crossLengthSquared > 1.0e-8f;
}

PlantTriangleHandle CPlantManagerSA::CreateTriangle(const CVector& v1, const CVector& v2, const CVector& v3, std::uint8_t surface, float density)
{
    if (!IsValidTriangle(v1, v2, v3) || !IsValidSurface(surface) || !std::isfinite(density) || density < 0.0f || density > MAX_CUSTOM_DENSITY)
        return nullptr;

    auto* triangle = *reinterpret_cast<CPlantLocTriSAInterface**>(VAR_UNUSED_LOC_TRI_LIST_HEAD);
    if (!triangle)
        return nullptr;

    using AddPlantTriangle = CPlantLocTriSAInterface*(__thiscall*)(CPlantLocTriSAInterface*, const CVector&, const CVector&, const CVector&, uint8, uint8, bool, bool);
    auto* result = reinterpret_cast<AddPlantTriangle>(FUNC_PLANT_LOC_TRI_ADD)(triangle, v1, v2, v3, surface, 0xFF, true, false);
    if (!result)
        return nullptr;

    for (auto& count : result->m_usMaxPlants)
    {
        const long scaled = std::lround(static_cast<double>(count) * static_cast<double>(density));
        count = static_cast<uint16>(std::clamp<long>(scaled, 0, std::numeric_limits<uint16>::max()));
    }

    return result;
}

void CPlantManagerSA::DestroyTriangle(PlantTriangleHandle handle)
{
    if (!handle)
        return;

    using ReleasePlantTriangle = void(__thiscall*)(CPlantLocTriSAInterface*);
    reinterpret_cast<ReleasePlantTriangle>(FUNC_PLANT_LOC_TRI_RELEASE)(static_cast<CPlantLocTriSAInterface*>(handle));
}

void CPlantManagerSA::RemoveAllPlants()
{
    while (true)
    {
        auto list = reinterpret_cast<CPlantColEntEntry**>(0xC0399C);
        if (*list == nullptr)
        {
            break;
        }

        (*list)->ReleaseEntry();
    }
}
