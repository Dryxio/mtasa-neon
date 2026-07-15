/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CIplStore.cpp
 *  PURPOSE:     IPL store class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"

#include "CIplStoreSA.h"
#include "CGameSA.h"
#include "CQuadTreeNodeSA.h"
#include <game/CIplBuildingRange.h>

static auto     gIplQuadTree = (CQuadTreeNodesSAInterface<CIplSAInterface>**)0x8E3FAC;
extern CGameSA* pGame;

CIplStoreSA::CIplStoreSA() : m_isStreamingEnabled(true), m_ppIplPoolInterface((CPoolSAInterface<CIplSAInterface>**)0x8E3FB0)
{
}

void CIplStoreSA::ClampBuildingRange(CIplSAInterface& ipl, std::size_t buildingPoolSize)
{
    const SIplBuildingRange range = ClampIplBuildingRange({ipl.minBuildId, ipl.maxBuildId}, buildingPoolSize);
    ipl.minBuildId = range.min;
    ipl.maxBuildId = range.max;
}

void CIplStoreSA::ClampBuildingRanges(std::size_t buildingPoolSize)
{
    if (!m_ppIplPoolInterface)
        return;

    auto* pool = *m_ppIplPoolInterface;
    if (!pool)
        return;

    // Every occupied IPL can outlive a server connection. After native unload
    // and a successful shrink, make all ranges safe before streaming is restored.
    for (int i = 0; i < pool->m_nSize; ++i)
    {
        if (!pool->IsContains(i))
            continue;

        if (auto* ipl = pool->GetObject(i))
            ClampBuildingRange(*ipl, buildingPoolSize);
    }
}

void CIplStoreSA::UnloadAndDisableStreaming(int iplId)
{
    // Is pool valid?
    if (!m_ppIplPoolInterface)
        return;

    // Is pool object valid?
    auto pool = *m_ppIplPoolInterface;
    if (!pool)
        return;

    // Is IPL in pool?
    if (!pool->IsContains(iplId))
        return;

    // Is IPL object valid?
    auto ipl = pool->GetObject(iplId);
    if (!ipl)
        return;

    // Shrinking an enlarged building pool during disconnect can discard slots
    // above the restored capacity while an IPL still retains its old inclusive
    // slot range. GTA trusts maxBuildId and walks past the resized allocation in
    // CStreaming::RemoveModel on the next reconnect. Keep the IPL metadata in
    // sync with the pool before handing it back to the native unload routine.
    const int buildingPoolSize = pGame->GetPools()->GetBuildingsPool().GetSize();
    ClampBuildingRange(*ipl, buildingPoolSize > 0 ? static_cast<std::size_t>(buildingPoolSize) : 0);

    typedef void*(__cdecl * Function_EnableStreaming)(int);
    ((Function_EnableStreaming)(0x405890))(iplId);
}

void CIplStoreSA::EnableStreaming(int iplId)
{
    // Is pool valid?
    if (!m_ppIplPoolInterface)
        return;

    // Is pool object valid?
    auto pool = *m_ppIplPoolInterface;
    if (!pool)
        return;

    // Is IPL in pool?
    if (!pool->IsContains(iplId))
        return;

    // Is IPL object valid?
    auto ipl = pool->GetObject(iplId);
    if (!ipl)
        return;

    ipl->bDisabledStreaming = false;

    if (*gIplQuadTree)
        (*gIplQuadTree)->AddItem(ipl, &ipl->rect);
}

void CIplStoreSA::SetDynamicIplStreamingEnabled(bool state)
{
    if (m_isStreamingEnabled == state)
        return;

    // Ipl with 0 index is generic
    // We don't unload this IPL

    auto pool = *m_ppIplPoolInterface;
    if (!pool)
        return;

    // Collect all IPL ids
    std::vector<int> iplIds;
    iplIds.reserve(pool->m_nSize);

    for (int i = 1; i < pool->m_nSize; i++)
    {
        if (pool->IsContains(i))
            iplIds.push_back(i);
    }

    // Now enable/disable streaming for all IPLs
    if (!state)
    {
        for (int iplId : iplIds)
            UnloadAndDisableStreaming(iplId);

        if (*gIplQuadTree)
            (*gIplQuadTree)->RemoveAllItems();
    }
    else
    {
        for (int iplId : iplIds)
            EnableStreaming(iplId);
    }

    m_isStreamingEnabled = state;
}

void CIplStoreSA::SetDynamicIplStreamingEnabled(bool state, std::function<bool(CIplSAInterface* ipl)> filter)
{
    if (m_isStreamingEnabled == state)
        return;

    // Ipl with 0 index is generic
    // We don't unload this IPL

    auto pool = *m_ppIplPoolInterface;
    if (!pool)
        return;

    // Collect IPL ids that match the filter
    std::vector<int> iplIds;
    iplIds.reserve(pool->m_nSize);

    for (int i = 1; i < pool->m_nSize; i++)
    {
        auto ipl = pool->GetObject(i);
        if (ipl && pool->IsContains(i) && filter(ipl))
            iplIds.push_back(i);
    }

    // Apply the streaming state change
    if (!state)
    {
        for (int iplId : iplIds)
            UnloadAndDisableStreaming(iplId);

        if (*gIplQuadTree)
            (*gIplQuadTree)->RemoveAllItems();
    }
    else
    {
        for (int iplId : iplIds)
            EnableStreaming(iplId);
    }

    m_isStreamingEnabled = state;
}
