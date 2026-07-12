/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CPickupSA.cpp
 *  PURPOSE:     Pickup entity
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CGameSA.h"
#include "CPickupSA.h"
#include "CWorldSA.h"

extern CGameSA* pGame;

namespace
{
    short CompressNativePickupCoordinate(float fCoordinate)
    {
        // The native fields are still needed by GTA's object factory, but converting an
        // out-of-range float directly to short is undefined and wrapped on the old build.
        constexpr float NATIVE_MIN = -32768.0f;
        constexpr float NATIVE_MAX = 32767.0f;
        const float     fScaledCoordinate = fCoordinate * 8.0f;
        if (!std::isfinite(fScaledCoordinate))
            return 0;
        return static_cast<short>(std::clamp(fScaledCoordinate, NATIVE_MIN, NATIVE_MAX));
    }

    bool RequiresExtendedPickupPosition(const CVector& vecPosition)
    {
        constexpr float NATIVE_MIN = -4096.0f;
        constexpr float NATIVE_MAX = 4095.875f;
        return vecPosition.fX < NATIVE_MIN || vecPosition.fX > NATIVE_MAX || vecPosition.fY < NATIVE_MIN || vecPosition.fY > NATIVE_MAX ||
               vecPosition.fZ < NATIVE_MIN || vecPosition.fZ > NATIVE_MAX;
    }
}  // namespace

CPickupSA::CPickupSA(CPickupSAInterface* pickupInterface)
{
    internalInterface = pickupInterface;
    object = 0;
    m_vecPosition = CVector(pickupInterface->CoorsX / 8.0f, pickupInterface->CoorsY / 8.0f, pickupInterface->CoorsZ / 8.0f);
    m_bHasExtendedPosition = false;
}

void CPickupSA::SetPosition(CVector* vecPosition)
{
    GetInterface()->bIsPickupNearby = 0;

    m_vecPosition = *vecPosition;
    m_bHasExtendedPosition = RequiresExtendedPickupPosition(*vecPosition);

    CPickupSAInterface* iPickup = GetInterface();
    iPickup->CoorsX = CompressNativePickupCoordinate(vecPosition->fX);
    iPickup->CoorsY = CompressNativePickupCoordinate(vecPosition->fY);
    iPickup->CoorsZ = CompressNativePickupCoordinate(vecPosition->fZ);
}

CVector* CPickupSA::GetPosition(CVector* vecPosition)
{
    if (m_bHasExtendedPosition)
    {
        *vecPosition = m_vecPosition;
    }
    else
    {
        CPickupSAInterface* iPickup = GetInterface();
        *vecPosition = CVector(iPickup->CoorsX / 8.0f, iPickup->CoorsY / 8.0f, iPickup->CoorsZ / 8.0f);
    }
    return vecPosition;
}

PickupType CPickupSA::GetType()
{
    return (PickupType)GetInterface()->Type;
}

void CPickupSA::SetType(PickupType type)
{
    GetInterface()->Type = (BYTE)type;
}

float CPickupSA::GetCurrentValue()
{
    return GetInterface()->CurrentValue;
}

void CPickupSA::SetCurrentValue(float fCurrentValue)
{
    GetInterface()->CurrentValue = fCurrentValue;
}

void CPickupSA::SetRegenerationTime(DWORD dwTime)
{
    GetInterface()->RegenerationTime = dwTime;
}

void CPickupSA::SetMoneyPerDay(WORD wMoneyPerDay)
{
    GetInterface()->MoneyPerDay = wMoneyPerDay;
}

WORD CPickupSA::GetMoneyPerDay()
{
    return GetInterface()->MoneyPerDay;
}

WORD CPickupSA::GetModel()
{
    return GetInterface()->MI;
}

void CPickupSA::SetModel(WORD wModelIndex)
{
    GetInterface()->MI = wModelIndex;
}

PickupState CPickupSA::GetState()
{
    return (PickupState)GetInterface()->State;
}

void CPickupSA::SetState(PickupState bState)
{
    GetInterface()->State = (BYTE)bState;
}

BYTE CPickupSA::GetAmmo()
{
    return GetInterface()->bNoAmmo;
}

void CPickupSA::SetAmmo(BYTE bAmmo)
{
    GetInterface()->bNoAmmo = bAmmo;
}

long CPickupSA::GetMonetaryValue()
{
    return GetInterface()->MonetaryValue;
}

void CPickupSA::SetMonetaryValue(long lMonetaryValue)
{
    GetInterface()->MonetaryValue = lMonetaryValue;
}

BYTE CPickupSA::IsNearby()
{
    return GetInterface()->bIsPickupNearby;
}

bool CPickupSA::GiveUsAPickUpObject(int ForcedObjectIndex)
{
    DWORD GiveUsAPickUpObject = FUNC_GIVEUSAPICKUP;
    DWORD dwObject = (DWORD) & (GetInterface()->pObject);
    DWORD dwThis = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        push    ForcedObjectIndex
        push    dwObject
        mov     ecx, dwThis
        call    GiveUsAPickUpObject
    }
    // clang-format on

    if (GetInterface()->pObject)
    {
        if (object)
        {
            ((CEntitySA*)object)->DoNotRemoveFromGame = true;
            delete object;
        }
        object = new CObjectSA(GetInterface()->pObject);

        // CPickup::GiveUsAPickUpObject initially positions the object from GTA's
        // compressed coordinates. Move it before CWorld::Add links it into a sector;
        // GTA pickup processing is disabled by MTA and cannot move it back afterwards.
        if (m_bHasExtendedPosition)
            object->SetPosition(&m_vecPosition);
        return true;
    }
    return false;
}

void CPickupSA::GetRidOfObjects()
{
    if (GetInterface()->pObject)
        ((CWorldSA*)pGame->GetWorld())->Remove(GetInterface()->pObject, CPickup_Destructor);

    if (object)
    {
        ((CEntitySA*)object)->DoNotRemoveFromGame = true;
        delete object;
        object = nullptr;
    }

    GetInterface()->pObject = nullptr;
}

void CPickupSA::Remove()
{
    DWORD dwFunc = FUNC_CPickup_Remove;
    DWORD dwThis = (DWORD)GetInterface();
    // clang-format off
    __asm
    {
        mov     ecx, dwThis
        call    dwFunc
    }
    // clang-format on

    // CPickup::Remove also destroys the owned object, so we need to delete our CObjectSA class
    if (object)
    {
        ((CEntitySA*)object)->DoNotRemoveFromGame = true;
        delete object;
        object = nullptr;
    }
}
