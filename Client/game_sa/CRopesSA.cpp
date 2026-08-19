/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CRopesSA.cpp
 *  PURPOSE:     Rope entity
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CRopesSA.h"

#include <algorithm>

DWORD dwDurationAddress = 0x558D1E;

CRopesSAInterface (&CRopesSA::ms_aRopes)[ROPES_COUNT] = *(CRopesSAInterface(*)[ROPES_COUNT])0xB768B8;

int CRopesSA::CreateRopeForSwatPed(const CVector& vecPosition, DWORD dwDuration)
{
    int      iReturn;
    DWORD    dwFunc = FUNC_CRopes_CreateRopeForSwatPed;
    CVector* pvecPosition = const_cast<CVector*>(&vecPosition);
    // First Push @ 0x558D1D is the duration.
    MemPut((void*)(dwDurationAddress), dwDuration);
    // clang-format off
    __asm
    {
        push    pvecPosition
        call    dwFunc
        add     esp, 0x4
        mov     iReturn, eax
    }
    // clang-format on
    // Set it back for SA in case we ever do some other implementation.
    MemPut((DWORD*)(dwDurationAddress), 4000);
    return iReturn;
}

bool CRopesSA::RegisterRope(std::uint32_t uiId, eRopeType type, const CVector& vecPosition, bool bExpires, std::uint8_t ucFixedNode,
                            bool bSitOnGround, CEntitySAInterface* pHolder, std::uint32_t uiLifetime)
{
    using RegisterFn = bool(__cdecl*)(std::uint32_t, std::uint32_t, CVector, bool, std::uint8_t, std::uint8_t, CEntitySAInterface*, std::uint32_t);
    auto fn = reinterpret_cast<RegisterFn>(FUNC_CRopes_RegisterRope);
    if (!fn(uiId, static_cast<std::uint32_t>(type), vecPosition, bExpires, ucFixedNode, bSitOnGround ? 1u : 0u, pHolder, uiLifetime))
        return false;

    // Managed ropes use an explicit attachElementToRope contract. Several stock
    // winch types otherwise scan nearby world entities and pick them up on their
    // own. GTA's byte at 0x326 is the native pickup cooldown; refreshing it on
    // every managed registration keeps autonomous pickup disabled while an
    // explicitly carried entity still follows the normal carried-object path.
    if (CRopesSAInterface* pRope = GetRope(uiId))
        pRope->m_ucWinchDisabled = 0xFF;
    return true;
}

CRopesSAInterface* CRopesSA::GetRope(std::uint32_t uiId)
{
    const int index = FindRope(uiId);
    return index >= 0 && index < ROPES_COUNT ? &ms_aRopes[index] : nullptr;
}

const CRopesSAInterface* CRopesSA::GetRope(std::uint32_t uiId) const
{
    const int index = FindRope(uiId);
    return index >= 0 && index < ROPES_COUNT ? &ms_aRopes[index] : nullptr;
}

int CRopesSA::FindRope(std::uint32_t uiId) const
{
    using FindFn = int(__cdecl*)(std::uint32_t);
    return reinterpret_cast<FindFn>(FUNC_CRopes_FindRope)(uiId);
}

bool CRopesSA::RemoveRope(std::uint32_t uiId)
{
    CRopesSAInterface* pRope = GetRope(uiId);
    if (!pRope)
        return false;

    using RemoveFn = void(__thiscall*)(CRopesSAInterface*);
    reinterpret_cast<RemoveFn>(FUNC_CRope_Remove)(pRope);
    return true;
}

void CRopesSA::RemoveEntityRope(CEntitySAInterface* pEntity)
{
    if (!pEntity)
        return;

    // Stock crane/object ropes use the holder pointer itself as their opaque ID.
    // Preserve that legacy cleanup behavior without assuming that every rope ID
    // is an entity pointer (managed ropes intentionally use generated IDs).
    const std::uint32_t entityId = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pEntity));
    RemoveRope(entityId);
}

bool CRopesSA::FindCoorsAlongRope(std::uint32_t uiId, float fProgress, CVector& vecPosition, CVector* pVelocity) const
{
    using FindCoorsFn = bool(__cdecl*)(std::uint32_t, float, CVector*, CVector*);
    return reinterpret_cast<FindCoorsFn>(FUNC_CRopes_FindCoorsAlongRope)(uiId, std::clamp(fProgress, 0.0f, 1.0f), &vecPosition, pVelocity);
}

bool CRopesSA::SetSpeedOfTopNode(std::uint32_t uiId, const CVector& vecVelocity)
{
    if (!GetRope(uiId))
        return false;

    using SetSpeedFn = void(__cdecl*)(std::uint32_t, CVector);
    reinterpret_cast<SetSpeedFn>(FUNC_CRopes_SetSpeedOfTopNode)(uiId, vecVelocity);
    return true;
}

bool CRopesSA::GetWinchHeight(std::uint32_t uiId, float& fHeight) const
{
    const CRopesSAInterface* pRope = GetRope(uiId);
    if (!pRope)
        return false;
    fHeight = pRope->m_fWinchHeight;
    return true;
}

bool CRopesSA::SetWinchHeight(std::uint32_t uiId, float fHeight)
{
    CRopesSAInterface* pRope = GetRope(uiId);
    if (!pRope || fHeight < 0.01f)
        return false;
    pRope->m_fWinchHeight = fHeight;
    return true;
}

bool CRopesSA::GetRopeLength(std::uint32_t uiId, float& fLength) const
{
    const CRopesSAInterface* pRope = GetRope(uiId);
    if (!pRope)
        return false;

    const unsigned int nodeCount = std::max(1u, 31u - static_cast<unsigned int>(std::min<std::uint8_t>(pRope->m_ucFixedNode, 30u)));
    fLength = pRope->m_fNativeSegmentLength * static_cast<float>(nodeCount);
    return true;
}

bool CRopesSA::SetRopeLength(std::uint32_t uiId, float fLength)
{
    CRopesSAInterface* pRope = GetRope(uiId);
    if (!pRope || fLength <= 0.0f)
        return false;

    const unsigned int nodeCount = std::max(1u, 31u - static_cast<unsigned int>(std::min<std::uint8_t>(pRope->m_ucFixedNode, 30u)));
    pRope->m_fNativeSegmentLength = fLength / static_cast<float>(nodeCount);
    return true;
}

bool CRopesSA::PickUpEntity(std::uint32_t uiId, CEntitySAInterface* pEntity)
{
    CRopesSAInterface* pRope = GetRope(uiId);
    if (!pRope || !pEntity || !pRope->m_pHookObject)
        return false;

    using PickUpFn = void(__thiscall*)(CRopesSAInterface*, CEntitySAInterface*);
    reinterpret_cast<PickUpFn>(FUNC_CRope_PickUpObject)(pRope, pEntity);
    return pRope->m_pCarriedEntity == pEntity;
}

bool CRopesSA::ReleasePickedUpEntity(std::uint32_t uiId)
{
    CRopesSAInterface* pRope = GetRope(uiId);
    if (!pRope)
        return false;
    if (!pRope->m_pCarriedEntity)
        return true;

    using ReleaseFn = void(__thiscall*)(CRopesSAInterface*);
    reinterpret_cast<ReleaseFn>(FUNC_CRope_ReleasePickedUpObject)(pRope);
    return pRope->m_pCarriedEntity == nullptr;
}

CEntitySAInterface* CRopesSA::GetPickedUpEntity(std::uint32_t uiId) const
{
    const CRopesSAInterface* pRope = GetRope(uiId);
    return pRope ? pRope->m_pCarriedEntity : nullptr;
}

bool CRopesSA::GetNode(std::uint32_t uiId, std::uint8_t ucNode, CVector& vecPosition, CVector& vecVelocity) const
{
    const CRopesSAInterface* pRope = GetRope(uiId);
    if (!pRope || ucNode >= 32)
        return false;

    vecPosition = pRope->m_vecNodes[ucNode];
    vecVelocity = pRope->m_vecNodeSpeeds[ucNode];
    return true;
}

unsigned int CRopesSA::GetFreeRopeCount() const
{
    unsigned int count = 0;
    for (const CRopesSAInterface& rope : ms_aRopes)
    {
        if (rope.m_ucRopeType == static_cast<std::uint8_t>(eRopeType::NONE))
            ++count;
    }
    return count;
}
