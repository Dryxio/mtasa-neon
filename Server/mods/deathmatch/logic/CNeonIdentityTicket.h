/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CNeonIdentityTicket.h
 *  PURPOSE:     Neon Identity connection-ticket verification
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <ctime>
#include <string>
#include <unordered_map>

struct SNeonIdentityClaims
{
    std::string accountId;
    std::string discordId;
    std::string serverEndpoint;
    std::string ticketId;
    std::time_t expiresAt{};
};

class CNeonIdentityTicketVerifier
{
public:
    static bool IsValidAccountId(const std::string& accountId) noexcept;
    static bool IsValidDiscordId(const std::string& discordId) noexcept;

    bool Configure(const std::string& issuer, const std::string& audience, const std::string& keyId, const std::string& publicKeyBase64Url, std::string& error);
    bool VerifyAndConsume(const std::string& ticket, SNeonIdentityClaims& claims, std::string& error);

private:
    void PruneReplayCache(std::time_t now);

    std::string                             m_issuer;
    std::string                             m_audience;
    std::string                             m_keyId;
    std::string                             m_publicKey;
    std::unordered_map<std::string, time_t> m_consumedTickets;
};
