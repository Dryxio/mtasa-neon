/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/packets/CPedSyncPacket.cpp
 *  PURPOSE:     Ped synchronization packet class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CPedSyncPacket.h"

CPedSyncPacket::CPedSyncPacket(SyncData& ReadData)
{
    // Copy the struct
    m_Syncs.push_back(ReadData);
}

bool CPedSyncPacket::Read(NetBitStreamInterface& BitStream)
{
    // While we're not out of bytes
    while (BitStream.GetNumberOfUnreadBits() > 32)
    {
        // Read out the sync data
        SyncData Data{};

        if (!BitStream.Read(Data.ID))
            return false;

        // Read the sync time context
        if (!BitStream.Read(Data.ucSyncTimeContext))
            return false;

        unsigned char ucFlags = 0;
        if (!BitStream.Read(ucFlags))
            return false;
        Data.ucFlags = ucFlags;

        if (!BitStream.Read(Data.flags2))
            return false;

        if (Data.flags2 & 0x02)
        {
            if (!BitStream.Can(eBitStreamVersion::NativeTaskLocomotionPresentation))
                Data.flags2 &= ~0x02;
            else if (!BitStream.Read(&Data.nativeTaskLocomotion))
                return false;
        }

        if (Data.flags2 & 0x04)
        {
            if (!BitStream.Can(eBitStreamVersion::NativeTaskWeaponPresentation))
                Data.flags2 &= ~0x04;
            else if (!BitStream.Read(&Data.nativeTaskWeaponPresentation))
                return false;
        }

        if (Data.flags2 & 0x08)
        {
            if (!BitStream.Can(eBitStreamVersion::NativeTaskAnimationPresentation))
                Data.flags2 &= ~0x08;
            else if (!BitStream.Read(&Data.nativeTaskAnimationPresentation))
                return false;
        }

        const bool nativeTaskAnimationLane = Data.flags2 & PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE;
        // This marker selects a separate sequencing channel; accepting any
        // spatial or unrelated presentation payload on it could let a newer
        // packet suppress ordinary state ordering. Keep the lane narrowly
        // scoped to animation snapshots.
        if (nativeTaskAnimationLane && (Data.ucFlags != 0 || Data.flags2 != (0x08 | PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE)))
            return false;
        if (!m_Syncs.empty() && nativeTaskAnimationLane != static_cast<bool>(m_Syncs.front().flags2 & PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE))
        {
            return false;
        }

        // Spatial updates are independently optional. In particular, a ped
        // turning in place sends rotation without position; skipping the
        // shared reader in that case leaves the relayed rotation at zero.
        if (ucFlags & 0x07)
        {
            if (!Data.ReadSpatialData(BitStream))
                return false;
        }

        // Health and armour
        if (ucFlags & 0x08)
        {
            if (!BitStream.Read(Data.fHealth))
                return false;
        }
        if (ucFlags & 0x10)
        {
            if (!BitStream.Read(Data.fArmor))
                return false;
        }

        if (Data.flags2 & 0x01)
        {
            SCameraRotationSync camRotation;
            if (!BitStream.Read(&camRotation))
                return false;
            Data.cameraRotation = camRotation.data.fRotation;
        }

        // On Fire
        if (ucFlags & 0x20)
        {
            if (!BitStream.ReadBit(Data.bOnFire))
                return false;
        }

        if (ucFlags & 0x60)
        {
            if (!BitStream.ReadBit(Data.isReloadingWeapon))
                return false;
        }

        // In Water
        if (ucFlags & 0x40)
        {
            if (!BitStream.ReadBit(Data.bIsInWater))
                return false;
        }

        // Add it to our list. We no longer check if it's valid here
        // because CPedSync does and it won't write bad ID's
        // back to clients.
        m_Syncs.push_back(Data);
    }

    return m_Syncs.size() > 0;
}

bool CPedSyncPacket::Write(NetBitStreamInterface& BitStream) const
{
    const SyncData& Data = m_Syncs.front();
    if (!&Data)
        return false;

    // Write vehicle ID
    BitStream.Write(Data.ID);

    // Write the sync time context
    BitStream.Write(Data.ucSyncTimeContext);

    std::uint8_t flags2 = Data.flags2;
    if (!BitStream.Can(eBitStreamVersion::NativeTaskLocomotionPresentation))
        flags2 &= ~0x02;
    if (!BitStream.Can(eBitStreamVersion::NativeTaskWeaponPresentation))
        flags2 &= ~0x04;
    if (!BitStream.Can(eBitStreamVersion::NativeTaskAnimationPresentation))
        flags2 &= ~(0x08 | PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE);
    if ((Data.flags2 & PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE) && !(flags2 & PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE))
        return false;

    BitStream.Write(Data.ucFlags);
    BitStream.Write(flags2);

    if (flags2 & 0x02)
        BitStream.Write(&Data.nativeTaskLocomotion);
    if (flags2 & 0x04)
        BitStream.Write(&Data.nativeTaskWeaponPresentation);
    if (flags2 & 0x08)
        BitStream.Write(&Data.nativeTaskAnimationPresentation);

    // Position and rotation
    if (Data.ucFlags & 0x01)
        BitStream.Write(&Data.position);

    if (Data.ucFlags & 0x02)
        BitStream.Write(&Data.rotation);

    // Velocity
    if (Data.ucFlags & 0x04)
        BitStream.Write(&Data.velocity);

    // Health, armour, on fire and is in water
    if (Data.ucFlags & 0x08)
        BitStream.Write(Data.fHealth);
    if (Data.ucFlags & 0x10)
        BitStream.Write(Data.fArmor);

    if (Data.flags2 & 0x01)
    {
        SCameraRotationSync camRotation;
        camRotation.data.fRotation = Data.cameraRotation;
        BitStream.Write(&camRotation);
    }

    if (Data.ucFlags & 0x20)
        BitStream.WriteBit(Data.bOnFire);
    if (Data.ucFlags & 0x60)
        BitStream.Write(Data.isReloadingWeapon);
    if (Data.ucFlags & 0x40)
        BitStream.Write(Data.bIsInWater);

    return true;
}

bool CPedSyncPacket::SyncData::ReadSpatialData(NetBitStreamInterface& BitStream)
{
    // Did we recieve position?
    if (ucFlags & 0x01)
    {
        if (!BitStream.Read(&position))
            return false;
    }

    // Rotation
    if (ucFlags & 0x02)
    {
        if (!BitStream.Read(&rotation))
            return false;
    }

    // Velocity
    if (ucFlags & 0x04)
    {
        if (!BitStream.Read(&velocity))
            return false;
    }

    return true;
}

bool CPedSyncPacket::SyncData::ReadSpatialDataBC(NetBitStreamInterface& BitStream)
{
    // Did we recieve position?
    if (ucFlags & 0x01)
    {
        if (!BitStream.Read(position.data.vecPosition.fX) || !BitStream.Read(position.data.vecPosition.fY) || !BitStream.Read(position.data.vecPosition.fZ))
            return false;
    }

    // Rotation
    if (ucFlags & 0x02)
    {
        if (!BitStream.Read(rotation.data.fRotation))
            return false;
    }

    // Velocity
    if (ucFlags & 0x04)
    {
        if (!BitStream.Read(velocity.data.vecVelocity.fX) || !BitStream.Read(velocity.data.vecVelocity.fY) || !BitStream.Read(velocity.data.vecVelocity.fZ))
            return false;
    }

    return true;
}
