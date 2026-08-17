/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/packets/CObjectStartSyncPacket.cpp
 *  PURPOSE:     Object start sync packet class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CObjectStartSyncPacket.h"
#include "CObject.h"
#include <net/SyncStructures.h>

bool CObjectStartSyncPacket::Write(NetBitStreamInterface& BitStream) const
{
    if (!m_pObject)
        return false;

    BitStream.Write(m_pObject->GetID());
    BitStream.WriteBit(m_pObject->IsDynamicPhysics());

    SPositionSync position;
    position.data.vecPosition = m_pObject->GetPosition();
    BitStream.Write(&position);

    SRotationRadiansSync rotation;
    m_pObject->GetRotation(rotation.data.vecRotation);
    BitStream.Write(&rotation);

    SVelocitySync velocity;
    velocity.data.vecVelocity = m_pObject->GetPhysicsVelocity();
    BitStream.Write(&velocity);

    SVelocitySync turnVelocity;
    turnVelocity.data.vecVelocity = m_pObject->GetPhysicsTurnVelocity();
    BitStream.Write(&turnVelocity);

    SObjectHealthSync health;
    health.data.fValue = m_pObject->GetHealth();
    BitStream.Write(&health);

    return true;
}
