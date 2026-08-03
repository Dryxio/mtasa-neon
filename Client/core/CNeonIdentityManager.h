/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CNeonIdentityManager.h
 *  PURPOSE:     Neon Identity OAuth session and connection-ticket manager
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <string>

struct SHttpDownloadResult;

class CNeonIdentityManager
{
public:
    enum class EPrepareResult
    {
        Anonymous,
        Ready,
        Pending,
    };

    CNeonIdentityManager();
    ~CNeonIdentityManager();

    void DoPulse();

    bool               StartDiscordSignIn();
    void               SignOut();
    bool               IsAuthenticated() const noexcept { return !m_sessionToken.empty(); }
    bool               IsSigningIn() const noexcept;
    const std::string& GetStatusText() const noexcept { return m_statusText; }
    const std::string& GetLastError() const noexcept { return m_lastError; }

    EPrepareResult PrepareConnectionTicket(const std::string& audience, const std::string& serverEndpoint);
    bool           IsTicketPreparationComplete() const noexcept { return m_ticketPreparationComplete; }
    bool           DidTicketPreparationSucceed() const noexcept { return m_ticketPreparationSucceeded; }
    std::string    ConsumePreparedConnectionTicket();
    void           CancelTicketPreparation();

private:
    enum class ERequestType
    {
        None,
        AuthStart,
        AuthPoll,
        Ticket,
    };

    enum class ESignInState
    {
        SignedOut,
        Starting,
        WaitingForBrowser,
        SignedIn,
        Failed,
    };

    static void StaticDownloadFinished(const SHttpDownloadResult& result);
    void        DownloadFinished(const SHttpDownloadResult& result);
    bool        QueueJsonRequest(ERequestType requestType, const char* path, const std::string& body, bool authenticated);
    void        HandleAuthStartResponse(const SHttpDownloadResult& result);
    void        HandleAuthPollResponse(const SHttpDownloadResult& result);
    void        HandleTicketResponse(const SHttpDownloadResult& result);
    void        FailSignIn(const std::string& error);
    void        FinishTicketPreparation(bool success, const std::string& error = {});

    bool        LoadSession();
    bool        SaveSession() const;
    void        DeleteSession() const;
    std::string GetSessionPath() const;
    std::string GetBaseUrl() const;

    ERequestType m_requestType{ERequestType::None};
    ESignInState m_signInState{ESignInState::SignedOut};
    std::string  m_sessionToken;
    std::string  m_flowId;
    std::string  m_pollToken;
    std::string  m_statusText{"Not signed in"};
    std::string  m_lastError;
    long long    m_nextPollAt{};

    bool        m_ticketPreparationPending{};
    bool        m_ticketPreparationComplete{};
    bool        m_ticketPreparationSucceeded{};
    std::string m_ticketAudience;
    std::string m_connectionTicket;
};
