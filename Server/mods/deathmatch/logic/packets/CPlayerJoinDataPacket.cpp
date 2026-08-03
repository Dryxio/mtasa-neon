/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/packets/CPlayerJoinDataPacket.cpp
 *  PURPOSE:     Player join data packet class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CPlayerJoinDataPacket.h"

bool CPlayerJoinDataPacket::Read(NetBitStreamInterface& BitStream)
{
    m_neonIdentityTicket.clear();

    // Read out the stuff
    if (!BitStream.Read(m_usNetVersion) || !BitStream.Read(m_usMTAVersion))
        return false;

    if (!BitStream.Read(m_usBitStreamVersion))
        return false;

    if (!BitStream.ReadString(m_strPlayerVersion))
        return false;

    if (!BitStream.ReadBit(m_bOptionalUpdateInfoRequired))
        return false;

    if (BitStream.Read(m_ucGameVersion) && BitStream.ReadStringCharacters(m_strNick, MAX_PLAYER_NICK_LENGTH) &&
        BitStream.Read(reinterpret_cast<char*>(&m_Password), 16))
    {
        // The original protocol reserved a fixed 32-byte community-login field
        // which has been empty for years. Consume it for wire compatibility,
        // then accept an optional trailing Neon ticket. Older clients have no
        // trailing bytes and older servers safely ignore the extension.
        char legacyCommunityId[MAX_SERIAL_LENGTH]{};
        if (!BitStream.Read(legacyCommunityId, sizeof(legacyCommunityId)))
            return false;

        // The historical packet ends on a non-byte boundary and the net
        // module reports its final padding bits as unread. Require a complete
        // string-length prefix before treating the extension as present.
        if (BitStream.GetNumberOfUnreadBits() >= static_cast<int>(sizeof(unsigned short) * 8) && !BitStream.ReadString(m_neonIdentityTicket))
            return false;
        if (m_neonIdentityTicket.size() > 4096)
            return false;

        // Shrink string sizes to fit
        m_strNick = *m_strNick;
        return true;
    }

    return false;
}
