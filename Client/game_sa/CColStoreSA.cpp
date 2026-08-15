/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CColStoreSA.cpp
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CColStoreSA.h"
#include "CGameSA.h"
#include <game/CPhysical.h>

extern CGameSA* pGame;

namespace
{
    constexpr DWORD FUNC_CColStore_LoadCollision = 0x410860;
    constexpr DWORD CALL_CColStore_LoadCollision[] = {0x40E855, 0x40E8FD, 0x40ED9E, 0x411E51, 0x6164AC};
    constexpr DWORD CALL_CPopulation_LoadCollision = 0x6164AC;

    CColStoreSA* g_collisionResidencyStore{};

    bool IsExpectedLoadCollisionCall(DWORD address)
    {
        if (*reinterpret_cast<const BYTE*>(address) != 0xE8)
            return false;

        const auto displacement = *reinterpret_cast<const std::int32_t*>(address + 1);
        return address + 5 + displacement == FUNC_CColStore_LoadCollision;
    }

    void __cdecl LoadCollisionWithAgentResidency(CVector position, bool ignorePlayerVehicle)
    {
        // GTA clears the required bits during LoadCollision. Applying leases
        // here, rather than in an element pulse, makes residence independent
        // of MTA/GTA frame ordering and covers future CPhysical agent types.
        if (g_collisionResidencyStore)
            g_collisionResidencyStore->ApplyCollisionResidencies();

        using Signature = void(__cdecl*)(CVector, bool);
        reinterpret_cast<Signature>(FUNC_CColStore_LoadCollision)(position, ignorePlayerVehicle);
    }
}

CColStoreSA::CColStoreSA()
{
    g_collisionResidencyStore = this;
    bool recurringCallInstalled = false;
    for (const DWORD call : CALL_CColStore_LoadCollision)
    {
        // Other MTA patches may already own an individual caller by the time
        // Game SA is initialised. Never make one such caller disable every
        // collision lease: hook each still-stock call independently. The
        // population call is the recurring per-frame guarantee required by
        // the lease contract; one-shot loading callers are only supplements.
        if (!IsExpectedLoadCollisionCall(call))
            continue;

        HookInstallCall(call, reinterpret_cast<DWORD>(&LoadCollisionWithAgentResidency));
        recurringCallInstalled = recurringCallInstalled || call == CALL_CPopulation_LoadCollision;
    }
    dassert(recurringCallInstalled);
    m_collisionResidencyHooksInstalled = recurringCallInstalled;
}

CColStoreSA::~CColStoreSA()
{
    m_collisionResidencies.clear();
    if (g_collisionResidencyStore == this)
        g_collisionResidencyStore = nullptr;
}

void CColStoreSA::Initialise()
{
    using Signature = void(__cdecl*)();
    const auto function = reinterpret_cast<Signature>(0x4113F0);
    function();
}

void CColStoreSA::Shutdown()
{
    // End every logical lease before GTA tears down collision storage. Entries
    // contain value snapshots, so shutdown order cannot dereference wrappers.
    m_collisionResidencies.clear();
    using Signature = void(__cdecl*)();
    const auto function = reinterpret_cast<Signature>(0x4114D0);
    function();
}

void CColStoreSA::BoundingBoxesPostProcess()
{
    using Signature = void(__cdecl*)();
    const auto function = reinterpret_cast<Signature>(0x410EC0);
    function();
}

int CColStoreSA::AddColSlot(const char* name)
{
    using Signature = int(__cdecl*)(const char*);
    const auto function = reinterpret_cast<Signature>(0x411140);
    return function(name);
}

bool CColStoreSA::IsValidSlot(CollisionSlot slot)
{
    // Native signature: bool __cdecl sub_410660(int a1)
    // Must use int to match GTA SA's ABI - it reads a full DWORD from stack
    using Signature = bool(__cdecl*)(int);
    const auto function = reinterpret_cast<Signature>(0x410660);
    return function(static_cast<int>(slot));
}

void CColStoreSA::AddCollisionNeededAtPosition(const CVector& position)
{
    using Signature = void(__cdecl*)(const CVector&);
    const auto function = reinterpret_cast<Signature>(0x4103A0);
    function(position);
}

void CColStoreSA::EnsureCollisionIsInMemory(const CVector& position)
{
    // Wait for GTA to complete initialization before calling collision functions
    // Race condition: MTA can trigger streaming/collision operations before GTA completes initialization.
    // GTA calls CTimer::Initialise at 0x53BDE6 during startup, which sets _timerFunction at 0x56189E.
    // If called before GTA reaches GS_INIT_PLAYING_GAME, the timer isn't initialized > crash at 0x5619E9 (CTimer::Suspend)

    if (!pGame || pGame->GetSystemState() < SystemState::GS_INIT_PLAYING_GAME)
        return;  // GTA not ready yet - skip (will retry on next streaming update)

    // Just in case
    constexpr auto ADDR_timerFunction = 0xB7CB28;
    const auto     timerFunction = *reinterpret_cast<void* const*>(ADDR_timerFunction);
    if (!timerFunction)
        return;  // Timer not initialized yet - skip

    // SA function signature: void __cdecl CColStore::EnsureCollisionIsInMemory(const CVector2D&)
    // CVector implicitly converts to CVector2D (uses x, y components only)
    using Signature = void(__cdecl*)(const CVector&);
    const auto function = reinterpret_cast<Signature>(0x410AD0);
    function(position);
}

bool CColStoreSA::HasCollisionLoaded(const CVector& position, int areaCode)
{
    using Signature = bool(__cdecl*)(const CVector&, int);
    const auto function = reinterpret_cast<Signature>(0x410CE0);
    return function(position, areaCode);
}

void CColStoreSA::RequestCollision(const CVector& position, int areaCode)
{
    using Signature = void(__cdecl*)(const CVector&, int);
    const auto function = reinterpret_cast<Signature>(0x410C00);
    function(position, areaCode);
}

void CColStoreSA::SetCollisionRequired(const CVector& position, int areaCode)
{
    using Signature = void(__cdecl*)(const CVector&, int);
    const auto function = reinterpret_cast<Signature>(0x4104E0);
    function(position, areaCode);
}

CollisionResidencyId CColStoreSA::AcquireCollisionResidency(CPhysical* physical, int areaCode)
{
    if (!m_collisionResidencyHooksInstalled || !physical)
        return 0;
    const CVector* position = physical->GetPosition();
    if (!position)
        return 0;

    CollisionResidencyId residency = m_nextCollisionResidency++;
    if (residency == 0)
        residency = m_nextCollisionResidency++;
    while (m_collisionResidencies.count(residency) != 0)
        residency = m_nextCollisionResidency++;

    m_collisionResidencies.emplace(residency, SCollisionResidency{*position, areaCode});
    RequestCollision(*position, areaCode);
    return residency;
}

bool CColStoreSA::UpdateCollisionResidency(CollisionResidencyId residency, CPhysical* physical, int areaCode)
{
    const auto found = m_collisionResidencies.find(residency);
    if (residency == 0 || !physical || found == m_collisionResidencies.end())
        return false;
    const CVector* position = physical->GetPosition();
    if (!position)
        return false;

    // Store a value snapshot, not the non-owning wrapper pointer. The hook can
    // run after an element pulse and must remain safe even if a caller misses
    // its explicit release during an exceptional teardown path.
    found->second = {*position, areaCode};
    return true;
}

void CColStoreSA::ReleaseCollisionResidency(CollisionResidencyId residency)
{
    m_collisionResidencies.erase(residency);
}

bool CColStoreSA::IsCollisionResidencyLoaded(CollisionResidencyId residency)
{
    const auto found = m_collisionResidencies.find(residency);
    return found != m_collisionResidencies.end() && HasCollisionLoaded(found->second.position, found->second.areaCode);
}

void CColStoreSA::ApplyCollisionResidencies()
{
    for (const auto& [residency, entry] : m_collisionResidencies)
    {
        dassert(residency != 0);
        SetCollisionRequired(entry.position, entry.areaCode);
    }
}

void CColStoreSA::RemoveAllCollision()
{
    using Signature = void(__cdecl*)();
    const auto function = reinterpret_cast<Signature>(0x410E00);
    function();
}

void CColStoreSA::AddRef(CollisionSlot slot)
{
    // Native signature: void __cdecl CColStore::AddRef(int)
    // Must use int to match GTA SA's ABI - it reads a full DWORD from stack
    using Signature = void(__cdecl*)(int);
    const auto function = reinterpret_cast<Signature>(0x4107A0);
    function(static_cast<int>(slot));
}

void CColStoreSA::RemoveRef(CollisionSlot slot)
{
    // Native signature: void __cdecl CColStore::RemoveRef(int)
    // Must use int to match GTA SA's ABI - it reads a full DWORD from stack
    using Signature = void(__cdecl*)(int);
    const auto function = reinterpret_cast<Signature>(0x4107D0);
    function(static_cast<int>(slot));
}

void CColStoreSA::RemoveCol(CollisionSlot slot)
{
    // Native expects int parameter
    using Signature = void(__cdecl*)(int);
    const auto function = reinterpret_cast<Signature>(0x410730);
    function(static_cast<int>(slot));
}

void CColStoreSA::RemoveColSlot(CollisionSlot slot)
{
    // Native expects int parameter
    using Signature = void(__cdecl*)(int);
    const auto function = reinterpret_cast<Signature>(0x411330);
    function(static_cast<int>(slot));
}

void CColStoreSA::LoadAllBoundingBoxes()
{
    using Signature = void(__cdecl*)();
    const auto function = reinterpret_cast<Signature>(0x4113D0);
    function();
}

CColStore::BoundingBox CColStoreSA::GetBoundingBox(CollisionSlot slot)
{
    // Native expects int parameter
    using Signature = BoundingBox&(__cdecl*)(int);
    const auto function = reinterpret_cast<Signature>(0x410800);
    return function(static_cast<int>(slot));
}

void CColStoreSA::IncludeModelIndex(CollisionSlot slot, std::uint16_t model)
{
    // Native expects int parameters
    using Signature = void(__cdecl*)(int, int);
    const auto function = reinterpret_cast<Signature>(0x410820);
    function(static_cast<int>(slot), static_cast<int>(model));
}

int CColStoreSA::GetFirstModel(CollisionSlot slot)
{
    // Native expects int parameter
    using Signature = int(__cdecl*)(int);
    const auto function = reinterpret_cast<Signature>(0x537A80);
    return function(static_cast<int>(slot));
}

int CColStoreSA::GetLastModel(CollisionSlot slot)
{
    // Native expects int parameter
    using Signature = int(__cdecl*)(int);
    const auto function = reinterpret_cast<Signature>(0x537AB0);
    return function(static_cast<int>(slot));
}
