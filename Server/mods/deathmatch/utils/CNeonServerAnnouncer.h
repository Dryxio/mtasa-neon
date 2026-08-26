/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto: San Andreas
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/utils/CNeonServerAnnouncer.h
 *  PURPOSE:     Neon public server registry heartbeat
 *
 *****************************************************************************/

#pragma once

class CMainConfig;
class CNeonServerIdentity;
class CNeonIdentityTicketVerifier;
struct SHttpDownloadResult;

class CNeonServerAnnouncer
{
public:
    CNeonServerAnnouncer(CMainConfig& config, CNeonServerIdentity* identity, CNeonIdentityTicketVerifier* ticketVerifier);
    ~CNeonServerAnnouncer();

    void Pulse();
    bool IsIdentityActive() const;

private:
    static void StaticDownloadFinished(const SHttpDownloadResult& result);
    void        DownloadFinished(const SHttpDownloadResult& result);
    SString     BuildHeartbeatBody() const;
    bool        IsEnabled() const;
    long long   RetryDelay(int statusCode);

    CMainConfig&                 m_config;
    CNeonServerIdentity*         m_identity{};
    CNeonIdentityTicketVerifier* m_ticketVerifier{};
    long long                    m_nextHeartbeatAt{};
    long long                    m_identityExpiresAt{};
    unsigned int                 m_consecutiveFailures{};
    bool                         m_requestPending{};
    bool                         m_registered{};
    bool                         m_loggedFailure{};
};
