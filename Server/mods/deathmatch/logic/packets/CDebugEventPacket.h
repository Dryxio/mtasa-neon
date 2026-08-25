/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Server/mods/deathmatch/logic/packets/CDebugEventPacket.h
 *  PURPOSE:     Lossless structured script diagnostic packet
 *
 *****************************************************************************/

#pragma once

#include "CPacket.h"
#include <net/SDebugEvent.h>

class CDebugEventPacket final : public CPacket
{
public:
    explicit CDebugEventPacket(const SDebugEvent& event) : m_event(event) {}

    ePacketID       GetPacketID() const override { return PACKET_ID_DEBUG_EVENT; }
    unsigned long   GetFlags() const override { return PACKET_HIGH_PRIORITY | PACKET_RELIABLE | PACKET_SEQUENCED; }
    ePacketOrdering GetPacketOrdering() const override { return PACKET_ORDERING_CHAT; }
    bool            Write(NetBitStreamInterface& bitStream) const override;

private:
    SDebugEvent m_event;
};
