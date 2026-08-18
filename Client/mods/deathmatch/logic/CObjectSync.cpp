/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CObjectSync.cpp
 *  PURPOSE:     Object sync class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "net/SyncStructures.h"
#include "CClientObjectPhysicsManager.h"

#include <unordered_map>

#ifdef WITH_OBJECT_SYNC

using std::list;

    #define OBJECT_SYNC_RATE (g_TickRateSettings.iObjectSync)

namespace
{
    struct SLastPhysicsSync
    {
        CVector vecVelocity;
        CVector vecTurnVelocity;
    };

    std::unordered_map<CDeathmatchObject*, SLastPhysicsSync> g_LastPhysicsSync;
}

CObjectSync::CObjectSync(CClientObjectManager* pObjectManager)
{
    m_pObjectManager = pObjectManager;
    m_ulLastSyncTime = 0;
}

bool CObjectSync::ProcessPacket(unsigned char ucPacketID, NetBitStreamInterface& BitStream)
{
    switch (ucPacketID)
    {
        case PACKET_ID_OBJECT_STARTSYNC:
            Packet_ObjectStartSync(BitStream);
            return true;
        case PACKET_ID_OBJECT_STOPSYNC:
            Packet_ObjectStopSync(BitStream);
            return true;
        case PACKET_ID_OBJECT_SYNC:
            Packet_ObjectSync(BitStream);
            return true;
    }

    return false;
}

void CObjectSync::DoPulse()
{
    CClientObjectPhysicsManager::Pulse();

    unsigned long ulCurrentTime = CClientTime::GetTime();
    if (ulCurrentTime >= m_ulLastSyncTime + OBJECT_SYNC_RATE)
    {
        Sync();
        m_ulLastSyncTime = ulCurrentTime;
    }
}

void CObjectSync::AddObject(CDeathmatchObject* pObject)
{
    m_List.push_front(pObject);

    if (CClientObjectPhysicsManager::IsEnabled(pObject))
    {
        SLastPhysicsSync& state = g_LastPhysicsSync[pObject];
        pObject->GetMoveSpeed(state.vecVelocity);
        pObject->GetTurnSpeed(state.vecTurnVelocity);
    }
}

void CObjectSync::RemoveObject(CDeathmatchObject* pObject)
{
    if (!m_List.empty())
        m_List.remove(pObject);
    g_LastPhysicsSync.erase(pObject);
}

void CObjectSync::ClearObjects()
{
    m_List.clear();
    g_LastPhysicsSync.clear();
}

bool CObjectSync::Exists(CDeathmatchObject* pObject)
{
    return m_List.Contains(pObject);
}

void CObjectSync::Packet_ObjectStartSync(NetBitStreamInterface& BitStream)
{
    ElementID ID;
    if (!BitStream.Read(ID))
        return;

    CDeathmatchObject* pObject = static_cast<CDeathmatchObject*>(m_pObjectManager->Get(ID));
    const bool         bDynamicPhysics = BitStream.ReadBit();

    SPositionSync        position;
    SRotationRadiansSync rotation;
    SVelocitySync        velocity;
    SVelocitySync        turnVelocity;
    SObjectHealthSync    health;
    if (!BitStream.Read(&position) || !BitStream.Read(&rotation) || !BitStream.Read(&velocity) || !BitStream.Read(&turnVelocity) || !BitStream.Read(&health))
        return;

    if (!pObject)
        return;

    CClientObjectPhysicsManager::SetEnabled(pObject, bDynamicPhysics);

    // Keep the long-standing attached-object orientation workaround intact.
    // Dynamic objects are already receiving authoritative snapshots before a
    // syncer migration, so only velocity needs to be restored here.
    pObject->SetMoveSpeed(velocity.data.vecVelocity);
    pObject->SetTurnSpeed(turnVelocity.data.vecVelocity);
    pObject->SetHealth(health.data.fValue);

    AddObject(pObject);
}

void CObjectSync::Packet_ObjectStopSync(NetBitStreamInterface& BitStream)
{
    ElementID ID;
    if (!BitStream.Read(ID))
        return;

    CDeathmatchObject* pObject = static_cast<CDeathmatchObject*>(m_pObjectManager->Get(ID));
    if (pObject)
        RemoveObject(pObject);
}

void CObjectSync::Packet_ObjectSync(NetBitStreamInterface& BitStream)
{
    while (BitStream.GetNumberOfUnreadBits() > 8)
    {
        ElementID ID;
        unsigned char ucSyncTimeContext;
        if (!BitStream.Read(ID) || !BitStream.Read(ucSyncTimeContext))
            return;

        SIntegerSync<unsigned char, 5> flags(0);
        if (!BitStream.Read(&flags))
            return;

        SPositionSync position;
        SRotationRadiansSync rotation;
        SObjectHealthSync health;
        SVelocitySync velocity;
        SVelocitySync turnVelocity;

        if ((flags & 0x1) && !BitStream.Read(&position))
            return;
        if ((flags & 0x2) && !BitStream.Read(&rotation))
            return;
        if ((flags & 0x4) && !BitStream.Read(&health))
            return;
        if ((flags & 0x8) && !BitStream.Read(&velocity))
            return;
        if ((flags & 0x10) && !BitStream.Read(&turnVelocity))
            return;

        CDeathmatchObject* pObject = static_cast<CDeathmatchObject*>(m_pObjectManager->Get(ID));
        if (!pObject || !pObject->CanUpdateSync(ucSyncTimeContext))
            continue;

        // Velocity fields are accepted by the server only for dynamic objects,
        // so they also act as a self-describing fallback for clients that joined
        // after the original SET_OBJECT_DYNAMIC_PHYSICS RPC.
        if ((flags & 0x18) && !CClientObjectPhysicsManager::IsEnabled(pObject))
            CClientObjectPhysicsManager::SetEnabled(pObject, true);

        if (flags & 0x1)
            pObject->SetPosition(position.data.vecPosition);
        if (flags & 0x2)
            pObject->SetRotationRadians(rotation.data.vecRotation);
        if (flags & 0x4)
            pObject->SetHealth(health.data.fValue);
        if (flags & 0x8)
            pObject->SetMoveSpeed(velocity.data.vecVelocity);
        if (flags & 0x10)
            pObject->SetTurnSpeed(turnVelocity.data.vecVelocity);
    }
}

void CObjectSync::Sync()
{
    if (m_List.empty())
        return;

    CBitStream bitStream;
    for (CDeathmatchObject* pObject : m_List)
        WriteObjectInformation(bitStream.pBitStream, pObject);

    g_pNet->SendPacket(PACKET_ID_OBJECT_SYNC, bitStream.pBitStream, PACKET_PRIORITY_MEDIUM, PACKET_RELIABILITY_UNRELIABLE_SEQUENCED);
}

void CObjectSync::WriteObjectInformation(NetBitStreamInterface* pBitStream, CDeathmatchObject* pObject)
{
    unsigned char ucFlags = 0;

    CVector vecPosition, vecRotation;
    pObject->GetPosition(vecPosition);
    pObject->GetRotationRadians(vecRotation);

    if (vecPosition != pObject->m_LastSyncedData.vecPosition)
        ucFlags |= 0x1;
    if (vecRotation != pObject->m_LastSyncedData.vecRotation)
        ucFlags |= 0x2;
    if (pObject->GetHealth() != pObject->m_LastSyncedData.fHealth)
        ucFlags |= 0x4;

    CVector vecVelocity;
    CVector vecTurnVelocity;
    if (CClientObjectPhysicsManager::IsEnabled(pObject))
    {
        pObject->GetMoveSpeed(vecVelocity);
        pObject->GetTurnSpeed(vecTurnVelocity);
        SLastPhysicsSync& state = g_LastPhysicsSync[pObject];
        if (vecVelocity != state.vecVelocity)
            ucFlags |= 0x8;
        if (vecTurnVelocity != state.vecTurnVelocity)
            ucFlags |= 0x10;
    }

    if (ucFlags == 0)
        return;

    pBitStream->Write(pObject->GetID());
    pBitStream->Write(pObject->GetSyncTimeContext());

    SIntegerSync<unsigned char, 5> flags(ucFlags);
    pBitStream->Write(&flags);

    if (ucFlags & 0x1)
    {
        SPositionSync position;
        position.data.vecPosition = vecPosition;
        pBitStream->Write(&position);
        pObject->m_LastSyncedData.vecPosition = vecPosition;
    }

    if (ucFlags & 0x2)
    {
        SRotationRadiansSync rotation;
        rotation.data.vecRotation = vecRotation;
        pBitStream->Write(&rotation);
        pObject->m_LastSyncedData.vecRotation = vecRotation;
    }

    if (ucFlags & 0x4)
    {
        SObjectHealthSync health;
        health.data.fValue = pObject->GetHealth();
        pBitStream->Write(&health);
        pObject->m_LastSyncedData.fHealth = health.data.fValue;
    }

    if (ucFlags & 0x8)
    {
        SVelocitySync velocity;
        velocity.data.vecVelocity = vecVelocity;
        pBitStream->Write(&velocity);
        g_LastPhysicsSync[pObject].vecVelocity = vecVelocity;
    }

    if (ucFlags & 0x10)
    {
        SVelocitySync turnVelocity;
        turnVelocity.data.vecVelocity = vecTurnVelocity;
        pBitStream->Write(&turnVelocity);
        g_LastPhysicsSync[pObject].vecTurnVelocity = vecTurnVelocity;
    }
}

#endif
