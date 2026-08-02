/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/packets/CPedStartSyncPacket.cpp
 *  PURPOSE:     Ped start synchronization packet class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CPedStartSyncPacket.h"
#include "CPed.h"

bool CPedStartSyncPacket::Write(NetBitStreamInterface& BitStream) const
{
    if (!m_pPed)
        return false;

    BitStream.Write(m_pPed->GetID());

    CVector vecTemp;

    vecTemp = m_pPed->GetPosition();
    BitStream.Write(vecTemp.fX);
    BitStream.Write(vecTemp.fY);
    BitStream.Write(vecTemp.fZ);

    BitStream.Write(m_pPed->GetRotation());

    m_pPed->GetVelocity(vecTemp);
    BitStream.Write(vecTemp.fX);
    BitStream.Write(vecTemp.fY);
    BitStream.Write(vecTemp.fZ);

    BitStream.Write(m_pPed->GetHealth());
    BitStream.Write(m_pPed->GetArmor());

    BitStream.Write(m_pPed->GetCameraRotation());

    // The new owner may not have been a near-viewer for the final animation
    // snapshot. Carry the cached physical takeover state so it can seed GTA's
    // in-air task or the exact climb anchor and phase directly.
    BitStream.Write(&m_pPed->GetNativeTaskPhysicalTakeover());

    return true;
}
