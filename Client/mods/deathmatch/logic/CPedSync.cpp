/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CPedSync.cpp
 *  PURPOSE:     Ped synchronization handler
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"

using std::list;

extern CClientGame* g_pClientGame;

#define PED_SYNC_RATE (g_TickRateSettings.iPedSync)

CPedSync::CPedSync(CClientPedManager* pPedManager)
{
    m_pPedManager = pPedManager;
    m_ulLastSyncTime = 0;
}

CPedSync::~CPedSync()
{
}

bool CPedSync::ProcessPacket(unsigned char ucPacketID, NetBitStreamInterface& BitStream)
{
    switch (ucPacketID)
    {
        case PACKET_ID_PED_STARTSYNC:
        {
            Packet_PedStartSync(BitStream);
            return true;
        }

        case PACKET_ID_PED_STOPSYNC:
        {
            Packet_PedStopSync(BitStream);
            return true;
        }

        case PACKET_ID_PED_SYNC:
        {
            Packet_PedSync(BitStream);
            return true;
        }
    }

    return false;
}

void CPedSync::DoPulse()
{
    // Got any items?
    if (m_List.size() > 0)
    {
        for (auto iter = m_List.begin(); iter != m_List.end(); ++iter)
        {
            CClientPed* pPed = *iter;
            // Update enter/exit sequence
            pPed->UpdateVehicleInOut();
        }
    }

    // Has it been long enough since our last state's sync?
    unsigned long ulCurrentTime = CClientTime::GetTime();
    if (ulCurrentTime >= m_ulLastSyncTime + PED_SYNC_RATE)
    {
        Update();
        m_ulLastSyncTime = ulCurrentTime;
    }
}

void CPedSync::AddPed(CClientPed* pPed)
{
    m_List.push_front(pPed);
    // The previous syncer may have left an active presentation sample on
    // viewers. Make the new owner publish its current state even when that
    // state is NONE, so ownership migration clears the old locomotion without
    // waiting for the receiver lease.
    pPed->m_LastSyncedData->nativeTaskLocomotionResetPending = true;
    pPed->m_LastSyncedData->nativeTaskWeaponPresentationResetPending = true;
    pPed->m_LastSyncedData->nativeTaskAnimationPresentationResetPending = true;
    pPed->SetSyncing(true);
}

void CPedSync::RemovePed(CClientPed* pPed)
{
    if (!m_List.empty())
        m_List.remove(pPed);

    pPed->SetSyncing(false);
}

bool CPedSync::Exists(CClientPed* pPed)
{
    return m_List.Contains(pPed);
}

void CPedSync::Packet_PedStartSync(NetBitStreamInterface& BitStream)
{
    // Read out the element id
    ElementID ID;
    if (BitStream.Read(ID))
    {
        // Grab the ped
        CClientPed* pPed = static_cast<CClientPed*>(m_pPedManager->Get(ID));
        if (pPed)
        {
            // Read out the position
            CVector vecPosition;
            BitStream.Read(vecPosition.fX);
            BitStream.Read(vecPosition.fY);
            BitStream.Read(vecPosition.fZ);

            // And rotation
            float fRotation;
            BitStream.Read(fRotation);

            // And the velocity
            CVector vecVelocity;
            BitStream.Read(vecVelocity.fX);
            BitStream.Read(vecVelocity.fY);
            BitStream.Read(vecVelocity.fZ);

            // And health/armor
            float fHealth, fArmor;
            BitStream.Read(fHealth);
            BitStream.Read(fArmor);

            float cameraRotation{};
            BitStream.Read(cameraRotation);
            pPed->SetCameraRotation(cameraRotation);

            // Set data
            pPed->SetPosition(vecPosition);
            pPed->SetCurrentRotation(fRotation);
            pPed->SetMoveSpeed(vecVelocity);

            // Unlock health and armour for the syncer
            pPed->UnlockHealth();
            pPed->UnlockArmor();

            // Set the new health
            pPed->SetHealth(fHealth);
            pPed->SetArmor(fArmor);

            AddPed(pPed);
        }
    }
}

void CPedSync::Packet_PedStopSync(NetBitStreamInterface& BitStream)
{
    // Read out the ped id
    ElementID ID;
    if (BitStream.Read(ID))
    {
        // Grab the ped
        CClientPed* pPed = static_cast<CClientPed*>(m_pPedManager->Get(ID));
        if (pPed)
        {
            // Lock health and armour
            pPed->LockHealth(pPed->GetHealth());
            pPed->LockArmor(pPed->GetArmor());

            // Stop syncing it
            RemovePed(pPed);
        }
    }
}

void CPedSync::Packet_PedSync(NetBitStreamInterface& BitStream)
{
    // While we're not out of peds
    while (BitStream.GetNumberOfUnreadBits() > 32)
    {
        // Read out the ped id
        ElementID ID;
        if (BitStream.Read(ID))
        {
            // Read out the sync time context. See CClientEntity for documentation on that.
            unsigned char ucSyncTimeContext = 0;
            BitStream.Read(ucSyncTimeContext);

            unsigned char ucFlags = 0;
            BitStream.Read(ucFlags);

            std::uint8_t flags2{};
            BitStream.Read(flags2);

            SNativeTaskLocomotionSync nativeTaskLocomotion;
            if ((flags2 & 0x02) && BitStream.Can(eBitStreamVersion::NativeTaskLocomotionPresentation) && !BitStream.Read(&nativeTaskLocomotion))
                return;

            SNativeTaskWeaponPresentationSync nativeTaskWeaponPresentation;
            if ((flags2 & 0x04) && BitStream.Can(eBitStreamVersion::NativeTaskWeaponPresentation) && !BitStream.Read(&nativeTaskWeaponPresentation))
                return;

            SNativeTaskAnimationPresentationSync nativeTaskAnimationPresentation;
            if ((flags2 & 0x08) && BitStream.Can(eBitStreamVersion::NativeTaskAnimationPresentation) && !BitStream.Read(&nativeTaskAnimationPresentation))
                return;

            CVector vecPosition{CVector::NoInit{}}, vecMoveSpeed{CVector::NoInit{}};
            float   fRotation, fHealth, fArmor;
            bool    bOnFire;
            bool    bIsInWater;
            float   cameraRotation;

            // Read out the position
            SPositionSync position(false);
            if (ucFlags & 0x01)
                BitStream.Read(&position);

            // And rotation
            SPedRotationSync rotation;
            if (ucFlags & 0x02)
                BitStream.Read(&rotation);

            // And the move speed
            SVelocitySync velocity;
            if (ucFlags & 0x04)
                BitStream.Read(&velocity);

            vecPosition = position.data.vecPosition;
            fRotation = rotation.data.fRotation;
            vecMoveSpeed = velocity.data.vecVelocity;

            // And health with armour
            if (ucFlags & 0x08)
                BitStream.Read(fHealth);
            if (ucFlags & 0x10)
                BitStream.Read(fArmor);

            if (flags2 & 0x01)
            {
                SCameraRotationSync camRotation;
                BitStream.Read(&camRotation);
                cameraRotation = camRotation.data.fRotation;
            }

            // And the burning state
            if (ucFlags & 0x20)
                BitStream.ReadBit(bOnFire);

            // And the in water state
            if (ucFlags & 0x40)
                BitStream.ReadBit(bIsInWater);

            // Grab the ped. Only update the sync if this packet is from the same context.
            CClientPed* pPed = m_pPedManager->Get(ID);
            if (pPed && pPed->CanUpdateSync(ucSyncTimeContext))
            {
                const bool lockSyncedAnimationTransform = pPed->HasSyncedAnim() && !pPed->GetAnimationCache().bUpdatePosition;
                if ((ucFlags & 0x01) && !lockSyncedAnimationTransform)
                    pPed->SetTargetPosition(vecPosition, PED_SYNC_RATE);
                if ((ucFlags & 0x02) && !lockSyncedAnimationTransform)
                    pPed->SetTargetRotation(PED_SYNC_RATE, fRotation, std::nullopt);
                if ((ucFlags & 0x04) && !lockSyncedAnimationTransform)
                    pPed->SetMoveSpeed(vecMoveSpeed);
                if (ucFlags & 0x08)
                    pPed->LockHealth(fHealth);
                if (ucFlags & 0x10)
                    pPed->LockArmor(fArmor);
                if ((flags2 & 0x01) && !lockSyncedAnimationTransform)
                    pPed->SetTargetRotation(PED_SYNC_RATE, std::nullopt, cameraRotation);
                if ((flags2 & 0x02) && BitStream.Can(eBitStreamVersion::NativeTaskLocomotionPresentation))
                    pPed->SetNativeTaskLocomotionPresentation(nativeTaskLocomotion, "ped_sync");
                if ((flags2 & 0x04) && BitStream.Can(eBitStreamVersion::NativeTaskWeaponPresentation))
                    pPed->SetNativeTaskWeaponPresentation(nativeTaskWeaponPresentation, "ped_sync");
                if ((flags2 & 0x08) && BitStream.Can(eBitStreamVersion::NativeTaskAnimationPresentation))
                    pPed->SetNativeTaskAnimationPresentation(nativeTaskAnimationPresentation, "ped_sync");
                // The syncer uses this bit when a synchronized animation ends
                // or is overridden. Mirror the cleanup locally; otherwise the
                // remote ped remains permanently excluded from native-task
                // locomotion presentation even though its anim task is gone.
                if (ucFlags & 0x80)
                {
                    pPed->KillAnimation();
                    pPed->SetHasSyncedAnim(false);
                    pPed->m_animationOverridedByClient = false;
                }
                if (ucFlags & 0x20)
                    pPed->SetOnFire(bOnFire);
                if (ucFlags & 0x40)
                    pPed->SetInWater(bIsInWater);
            }
        }
    }
}

void CPedSync::Update()
{
    // Got any items?
    if (m_List.size() > 0)
    {
        // Create a packet
        NetBitStreamInterface* pBitStream = g_pNet->AllocateNetBitStream();
        if (pBitStream)
        {
            // Write each ped to it
            list<CClientPed*>::const_iterator iter = m_List.begin();
            for (; iter != m_List.end(); ++iter)
            {
                WritePedInformation(pBitStream, *iter);
            }

            // Send and destroy the packet
            g_pNet->SendPacket(PACKET_ID_PED_SYNC, pBitStream, PACKET_PRIORITY_MEDIUM, PACKET_RELIABILITY_UNRELIABLE_SEQUENCED);
            g_pNet->DeallocateNetBitStream(pBitStream);
        }
    }
}

void CPedSync::WritePedInformation(NetBitStreamInterface* pBitStream, CClientPed* pPed)
{
    CVector vecPosition;
    pPed->GetPosition(vecPosition);
    CVector vecVelocity;
    pPed->GetMoveSpeed(vecVelocity);

    // Server-authored named animations without root motion must keep the
    // transform that accompanied their RPC. GTA can reset its transient
    // current-rotation field while such an animation is running even though
    // the rendered matrix is unchanged; publishing that field turns remote
    // actors toward zero and also corrupts the server's authoritative state.
    const bool lockSyncedAnimationTransform = pPed->HasSyncedAnim() && !pPed->GetAnimationCache().bUpdatePosition;

    unsigned char ucFlags = 0;
    if (!lockSyncedAnimationTransform && vecPosition != pPed->m_LastSyncedData->vPosition)
        ucFlags |= 0x01;
    if (!lockSyncedAnimationTransform && pPed->GetCurrentRotation() != pPed->m_LastSyncedData->fRotation)
        ucFlags |= 0x02;
    if (!lockSyncedAnimationTransform && vecVelocity != pPed->m_LastSyncedData->vVelocity)
        ucFlags |= 0x04;
    if (pPed->GetHealth() != pPed->m_LastSyncedData->fHealth)
        ucFlags |= 0x08;
    if (pPed->GetArmor() != pPed->m_LastSyncedData->fArmour)
        ucFlags |= 0x10;
    if (pPed->IsOnFire() != pPed->m_LastSyncedData->bOnFire)
        ucFlags |= 0x20;
    if (pPed->IsInWater() != pPed->m_LastSyncedData->bIsInWater)
        ucFlags |= 0x40;
    if (pPed->IsReloadingWeapon() != pPed->m_LastSyncedData->isReloadingWeapon)
        ucFlags |= 0x60;
    if (pPed->HasSyncedAnim() && (!pPed->IsRunningAnimation() || pPed->m_animationOverridedByClient))
        ucFlags |= 0x80;

    std::uint8_t flags2{};
    if (!lockSyncedAnimationTransform && !IsNearlyEqual(pPed->GetCameraRotation(), pPed->m_LastSyncedData->cameraRotation))
        flags2 |= 0x01;

    const SNativeTaskLocomotionSync  nativeTaskLocomotion = pPed->GetNativeTaskLocomotion();
    const SNativeTaskLocomotionSync& lastNativeTaskLocomotion = pPed->m_LastSyncedData->nativeTaskLocomotion;
    const bool                       nativeTaskLocomotionChanged = nativeTaskLocomotion.data.uiMode != lastNativeTaskLocomotion.data.uiMode ||
                                             nativeTaskLocomotion.data.sLeftStickX != lastNativeTaskLocomotion.data.sLeftStickX ||
                                             nativeTaskLocomotion.data.sLeftStickY != lastNativeTaskLocomotion.data.sLeftStickY;
    if (pBitStream->Can(eBitStreamVersion::NativeTaskLocomotionPresentation) &&
        (nativeTaskLocomotion.data.uiMode != SNativeTaskLocomotionSync::NONE || nativeTaskLocomotionChanged ||
         pPed->m_LastSyncedData->nativeTaskLocomotionResetPending))
    {
        flags2 |= 0x02;
    }

    const SNativeTaskWeaponPresentationSync  nativeTaskWeaponPresentation = pPed->GetNativeTaskWeaponPresentation();
    const SNativeTaskWeaponPresentationSync& lastNativeTaskWeaponPresentation = pPed->m_LastSyncedData->nativeTaskWeaponPresentation;
    const bool nativeTaskWeaponPresentationChanged = nativeTaskWeaponPresentation.data.uiMode != lastNativeTaskWeaponPresentation.data.uiMode ||
                                                     nativeTaskWeaponPresentation.data.ucWeaponType != lastNativeTaskWeaponPresentation.data.ucWeaponType ||
                                                     nativeTaskWeaponPresentation.data.usBurstLength != lastNativeTaskWeaponPresentation.data.usBurstLength ||
                                                     nativeTaskWeaponPresentation.data.ucShootingRate != lastNativeTaskWeaponPresentation.data.ucShootingRate ||
                                                     nativeTaskWeaponPresentation.data.vecTarget != lastNativeTaskWeaponPresentation.data.vecTarget;
    if (pBitStream->Can(eBitStreamVersion::NativeTaskWeaponPresentation) &&
        (nativeTaskWeaponPresentation.data.uiMode != SNativeTaskWeaponPresentationSync::NONE || nativeTaskWeaponPresentationChanged ||
         pPed->m_LastSyncedData->nativeTaskWeaponPresentationResetPending))
    {
        flags2 |= 0x04;
    }

    const SNativeTaskAnimationPresentationSync  nativeTaskAnimationPresentation = pPed->GetNativeTaskAnimationPresentation();
    const SNativeTaskAnimationPresentationSync& lastNativeTaskAnimationPresentation = pPed->m_LastSyncedData->nativeTaskAnimationPresentation;
    const bool                                  nativeTaskAnimationPresentationChanged =
        nativeTaskAnimationPresentation.data.uiMode != lastNativeTaskAnimationPresentation.data.uiMode ||
        nativeTaskAnimationPresentation.data.usAnimGroup != lastNativeTaskAnimationPresentation.data.usAnimGroup ||
        nativeTaskAnimationPresentation.data.usAnimId != lastNativeTaskAnimationPresentation.data.usAnimId ||
        nativeTaskAnimationPresentation.data.fProgress != lastNativeTaskAnimationPresentation.data.fProgress ||
        nativeTaskAnimationPresentation.data.fSpeed != lastNativeTaskAnimationPresentation.data.fSpeed ||
        nativeTaskAnimationPresentation.data.fBlendAmount != lastNativeTaskAnimationPresentation.data.fBlendAmount;
    if (pBitStream->Can(eBitStreamVersion::NativeTaskAnimationPresentation) &&
        (nativeTaskAnimationPresentation.data.uiMode != SNativeTaskAnimationPresentationSync::NONE || nativeTaskAnimationPresentationChanged ||
         pPed->m_LastSyncedData->nativeTaskAnimationPresentationResetPending))
    {
        flags2 |= 0x08;
    }

    // Do we really have to sync this ped?
    if (ucFlags == 0 && flags2 == 0)
        return;

    // Write the ped id
    pBitStream->Write(pPed->GetID());

    // Write the sync time context
    pBitStream->Write(pPed->GetSyncTimeContext());

    // Write flags
    pBitStream->Write(ucFlags);

    // Write flags 2
    pBitStream->Write(flags2);

    if (flags2 & 0x02)
    {
        pBitStream->Write(&nativeTaskLocomotion);
        pPed->m_LastSyncedData->nativeTaskLocomotion = nativeTaskLocomotion;
        pPed->m_LastSyncedData->nativeTaskLocomotionResetPending = false;
    }

    if (flags2 & 0x04)
    {
        pBitStream->Write(&nativeTaskWeaponPresentation);
        pPed->m_LastSyncedData->nativeTaskWeaponPresentation = nativeTaskWeaponPresentation;
        pPed->m_LastSyncedData->nativeTaskWeaponPresentationResetPending = false;
    }

    if (flags2 & 0x08)
    {
        pBitStream->Write(&nativeTaskAnimationPresentation);
        pPed->m_LastSyncedData->nativeTaskAnimationPresentation = nativeTaskAnimationPresentation;
        pPed->m_LastSyncedData->nativeTaskAnimationPresentationResetPending = false;
    }

    // Write position if needed
    if (ucFlags & 0x01)
    {
        SPositionSync position(false);
        position.data.vecPosition = vecPosition;
        pBitStream->Write(&position);

        pPed->m_LastSyncedData->vPosition = vecPosition;
    }

    if (ucFlags & 0x02)
    {
        SPedRotationSync rotation;
        rotation.data.fRotation = pPed->GetCurrentRotation();
        pBitStream->Write(&rotation);

        pPed->m_LastSyncedData->fRotation = pPed->GetCurrentRotation();
    }

    // Write velocity
    if (ucFlags & 0x04)
    {
        SVelocitySync velocity;
        pBitStream->Write(&velocity);

        pPed->m_LastSyncedData->vVelocity = vecVelocity;
    }

    // And health
    if (ucFlags & 0x08)
    {
        pBitStream->Write(pPed->GetHealth());
        pPed->m_LastSyncedData->fHealth = pPed->GetHealth();
    }

    if (ucFlags & 0x10)
    {
        pBitStream->Write(pPed->GetArmor());
        pPed->m_LastSyncedData->fArmour = pPed->GetArmor();
    }

    if (flags2 & 0x01)
    {
        SCameraRotationSync camRotation;
        camRotation.data.fRotation = pPed->GetCameraRotation();
        pBitStream->Write(&camRotation);
        pPed->m_LastSyncedData->cameraRotation = camRotation.data.fRotation;
    }

    if (ucFlags & 0x20)
    {
        pBitStream->WriteBit(pPed->IsOnFire());
        pPed->m_LastSyncedData->bOnFire = pPed->IsOnFire();
    }

    if (ucFlags & 0x40)
    {
        pBitStream->WriteBit(pPed->IsInWater());
        pPed->m_LastSyncedData->bIsInWater = pPed->IsInWater();
    }

    if (ucFlags & 0x60)
    {
        bool isReloadingWeapon = pPed->IsReloadingWeapon();

        pBitStream->WriteBit(isReloadingWeapon);
        pPed->m_LastSyncedData->isReloadingWeapon = isReloadingWeapon;
    }

    // The animation has been overwritten or interrupted by the client
    if (ucFlags & 0x80)
    {
        pPed->SetHasSyncedAnim(false);
        pPed->m_animationOverridedByClient = false;
    }
}
