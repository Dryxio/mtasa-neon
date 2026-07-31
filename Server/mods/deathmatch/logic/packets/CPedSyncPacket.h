/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/packets/CPedSyncPacket.h
 *  PURPOSE:     Ped synchronization packet class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <CVector.h>
#include "CPacket.h"
#include <net/SyncStructures.h>
#include <vector>

class CPedSyncPacket final : public CPacket
{
public:
    struct SyncData
    {
        ElementID                            ID;
        unsigned char                        ucFlags;
        std::uint8_t                         flags2;
        unsigned char                        ucSyncTimeContext;
        SPositionSync                        position;
        SPedRotationSync                     rotation;
        SVelocitySync                        velocity;
        float                                fHealth;
        float                                fArmor;
        bool                                 bOnFire;
        bool                                 bIsInWater;
        bool                                 isReloadingWeapon;
        float                                cameraRotation;
        SNativeTaskLocomotionSync            nativeTaskLocomotion;
        SNativeTaskWeaponPresentationSync    nativeTaskWeaponPresentation;
        SNativeTaskAnimationPresentationSync nativeTaskAnimationPresentation;

        bool ReadSpatialData(NetBitStreamInterface& BitStream);
        // Backward compatibility
        bool ReadSpatialDataBC(NetBitStreamInterface& BitStream);
    };

public:
    // Used when receiving ped sync from clients, can contain multiple SyncData
    CPedSyncPacket() {};
    // Used when sending ped sync to clients, only contains one SyncData
    CPedSyncPacket(SyncData& pReadData);

    ePacketID       GetPacketID() const { return PACKET_ID_PED_SYNC; };
    unsigned long   GetFlags() const { return PACKET_MEDIUM_PRIORITY | PACKET_SEQUENCED; };
    ePacketOrdering GetPacketOrdering() const override
    {
        return !m_Syncs.empty() && (m_Syncs.front().flags2 & PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE) ? PACKET_ORDERING_NATIVE_TASK_PRESENTATION
                                                                                                        : PACKET_ORDERING_DEFAULT;
    }

    bool Read(NetBitStreamInterface& BitStream);
    bool Write(NetBitStreamInterface& BitStream) const;

    std::vector<SyncData> m_Syncs;
};
