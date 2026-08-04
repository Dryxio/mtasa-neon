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
struct SHttpDownloadResult;

class CNeonServerAnnouncer
{
public:
    explicit CNeonServerAnnouncer(CMainConfig& config);
    ~CNeonServerAnnouncer();

    void Pulse();

private:
    static void StaticDownloadFinished(const SHttpDownloadResult& result);
    void        DownloadFinished(const SHttpDownloadResult& result);
    SString     BuildHeartbeatBody() const;
    bool        IsEnabled() const;

    CMainConfig& m_config;
    long long    m_nextHeartbeatAt{};
    bool         m_requestPending{};
    bool         m_registered{};
    bool         m_loggedFailure{};
};
