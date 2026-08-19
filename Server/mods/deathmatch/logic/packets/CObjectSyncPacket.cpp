/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/packets/CObjectSyncPacket.cpp
 *  PURPOSE:     Object sync packet class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CObjectSyncPacket.h"
#include <net/SyncStructures.h>

CObjectSyncPacket::~CObjectSyncPacket()
{
    std::vector<SyncData*>::const_iterator iter = m_Syncs.begin();
    for (; iter != m_Syncs.end(); ++iter)
        delete *iter;
    m_Syncs.clear();
}

bool CObjectSyncPacket::Read(NetBitStreamInterface& BitStream)
{
    while (BitStream.GetNumberOfUnreadBits() > 8)
    {
        SyncData* pData = new SyncData;
        pData->bSend = false;

        if (!BitStream.Read(pData->ID) || !BitStream.Read(pData->ucSyncTimeContext))
        {
            delete pData;
            return false;
        }

        SIntegerSync<unsigned char, 5> flags;
        if (!BitStream.Read(&flags))
        {
            delete pData;
            return false;
        }
        pData->ucFlags = flags;

        if (flags & 0x1)
        {
            SPositionSync position;
            if (!BitStream.Read(&position))
            {
                delete pData;
                return false;
            }
            pData->vecPosition = position.data.vecPosition;
        }

        if (flags & 0x2)
        {
            SRotationRadiansSync rotation;
            if (!BitStream.Read(&rotation))
            {
                delete pData;
                return false;
            }
            pData->vecRotation = rotation.data.vecRotation;
        }

        if (flags & 0x4)
        {
            SObjectHealthSync health;
            if (!BitStream.Read(&health))
            {
                delete pData;
                return false;
            }
            pData->fHealth = health.data.fValue;
        }

        if (flags & 0x8)
        {
            SVelocitySync velocity;
            if (!BitStream.Read(&velocity))
            {
                delete pData;
                return false;
            }
            pData->vecVelocity = velocity.data.vecVelocity;
        }

        if (flags & 0x10)
        {
            SVelocitySync turnVelocity;
            if (!BitStream.Read(&turnVelocity))
            {
                delete pData;
                return false;
            }
            pData->vecTurnVelocity = turnVelocity.data.vecVelocity;
        }

        m_Syncs.push_back(pData);
    }

    return !m_Syncs.empty();
}

bool CObjectSyncPacket::Write(NetBitStreamInterface& BitStream) const
{
    bool bSent = false;
    for (SyncData* pData : m_Syncs)
    {
        if (!pData->bSend || pData->ucFlags == 0)
            continue;

        BitStream.Write(pData->ID);
        BitStream.Write(pData->ucSyncTimeContext);

        SIntegerSync<unsigned char, 5> flags(pData->ucFlags);
        BitStream.Write(&flags);

        if (flags & 0x1)
        {
            SPositionSync position;
            position.data.vecPosition = pData->vecPosition;
            BitStream.Write(&position);
        }

        if (flags & 0x2)
        {
            SRotationRadiansSync rotation;
            rotation.data.vecRotation = pData->vecRotation;
            BitStream.Write(&rotation);
        }

        if (flags & 0x4)
        {
            SObjectHealthSync health;
            health.data.fValue = pData->fHealth;
            BitStream.Write(&health);
        }

        if (flags & 0x8)
        {
            SVelocitySync velocity;
            velocity.data.vecVelocity = pData->vecVelocity;
            BitStream.Write(&velocity);
        }

        if (flags & 0x10)
        {
            SVelocitySync turnVelocity;
            turnVelocity.data.vecVelocity = pData->vecTurnVelocity;
            BitStream.Write(&turnVelocity);
        }

        bSent = true;
    }

    return bSent;
}
