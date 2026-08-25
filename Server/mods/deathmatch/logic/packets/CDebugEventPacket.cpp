/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Server/mods/deathmatch/logic/packets/CDebugEventPacket.cpp
 *  PURPOSE:     Lossless structured script diagnostic packet
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CDebugEventPacket.h"

bool CDebugEventPacket::Write(NetBitStreamInterface& bitStream) const
{
    WriteDebugEvent(bitStream, m_event);
    return true;
}
