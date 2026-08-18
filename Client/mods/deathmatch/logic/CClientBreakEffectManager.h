/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientBreakEffectManager.h
 *  PURPOSE:     Generic RenderWare mesh fracture cache, physics and rendering
 *
 *****************************************************************************/

#pragma once

#include "CClientEntity.h"
#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

struct RpGeometry;
class CClientBreakEffect;
class CClientManager;
class CClientObject;

struct SManagedBreakOptions
{
    std::size_t   fragments = 0; // 0 = automatic
    float         force = 5.0f;
    float         randomness = 1.5f;
    CVector       velocity{0.0f, 0.0f, 1.0f};
    CVector       impactPosition;
    bool          hasImpactPosition = false;
    std::uint32_t lifetimeMs = 8000;
    float         gravity = 9.81f;
    float         bounce = 0.35f;
    float         drag = 0.12f;
    float         renderDistance = 350.0f;
    std::uint32_t seed = 0x4E454F4Eu;
    bool          hideOriginal = true;
    bool          disableOriginalCollision = true;
};

class CClientBreakEffectManager
{
    friend class CClientBreakEffect;

public:
    static CClientBreakEffectManager& GetSingleton();

    CClientBreakEffect* CreateFromObject(CClientManager* pManager, CClientObject* pObject, ElementID ID, const SManagedBreakOptions& options);
    CClientBreakEffect* Get(ElementID ID);

    void DoPulse(CClientManager* pManager);
    void DoRender(CClientManager* pManager);
    void DoRenderQueued(CClientManager* pManager);

    std::size_t GetCount() const { return m_List.size(); }
    std::size_t GetActiveFragmentCount() const;
    std::size_t GetCacheEntryCount() const { return m_Cache.size(); }
    void        ClearCache();

    static constexpr std::size_t MAX_ACTIVE_EFFECTS = 64;
    static constexpr std::size_t MAX_ACTIVE_FRAGMENTS = 512;
    static constexpr std::size_t MAX_FRAGMENTS_PER_EFFECT = 64;

private:
    struct SClusterCacheEntry
    {
        const RpGeometry*       geometry = nullptr;
        int                     triangleCount = 0;
        int                     vertexCount = 0;
        std::size_t             fragments = 0;
        std::uint32_t           seed = 0;
        std::uint64_t           signature = 0;
        std::vector<std::uint16_t> assignments;
    };

    CClientBreakEffectManager() = default;

    void AddToList(CClientBreakEffect* pEffect);
    void RemoveFromList(CClientBreakEffect* pEffect);

    bool BuildClusterAssignments(const RpGeometry* geometry, std::size_t fragments, std::uint32_t seed,
                                 std::vector<std::uint16_t>& outAssignments, std::size_t& outClusterCount, bool& outCacheHit);
    std::uint64_t ComputeGeometrySignature(const RpGeometry* geometry) const;

    std::list<CClientBreakEffect*>   m_List;
    std::vector<SClusterCacheEntry>  m_Cache;
    unsigned long long               m_ullLastPulse = 0;
    std::uint32_t                    m_uiPhysicsFrame = 0;
};
