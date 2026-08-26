/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CNeonIdentityManager.cpp
 *  PURPOSE:     Neon Identity OAuth session and connection-ticket manager
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNeonIdentityManager.h"

#include <json.h>

namespace
{
    constexpr char        SESSION_MAGIC[] = "NEONID1";
    constexpr char        DPAPI_ENTROPY[] = "neon-identity-session-v1";
    constexpr std::size_t MAX_RESPONSE_BYTES = 64 * 1024;
    constexpr std::size_t MAX_TOKEN_BYTES = 4096;
    constexpr long long   POLL_INTERVAL_MS = 1500;

    class CJsonObject
    {
    public:
        explicit CJsonObject(json_object* object = nullptr) : m_object(object) {}
        ~CJsonObject()
        {
            if (m_object)
                json_object_put(m_object);
        }

        CJsonObject(const CJsonObject&) = delete;
        CJsonObject& operator=(const CJsonObject&) = delete;

        json_object* Get() const { return m_object; }

    private:
        json_object* m_object;
    };

    CJsonObject ParseResponse(const SHttpDownloadResult& result)
    {
        if (!result.bSuccess || !result.pData || result.dataSize == 0 || result.dataSize > MAX_RESPONSE_BYTES)
            return CJsonObject();

        json_tokener* tokener = json_tokener_new();
        if (!tokener)
            return CJsonObject();

        json_object* object = json_tokener_parse_ex(tokener, result.pData, static_cast<int>(result.dataSize));
        const bool   valid = json_tokener_get_error(tokener) == json_tokener_success && object && json_tokener_get_parse_end(tokener) == result.dataSize;
        json_tokener_free(tokener);
        if (!valid)
        {
            if (object)
                json_object_put(object);
            return CJsonObject();
        }
        return CJsonObject(object);
    }

    bool ReadString(json_object* object, const char* name, std::string& value, std::size_t maxLength = MAX_TOKEN_BYTES)
    {
        json_object* field = nullptr;
        if (!object || !json_object_object_get_ex(object, name, &field) || !field || json_object_get_type(field) != json_type_string)
            return false;

        const char* text = json_object_get_string(field);
        const int   length = json_object_get_string_len(field);
        if (!text || length <= 0 || static_cast<std::size_t>(length) > maxLength)
            return false;

        value.assign(text, static_cast<std::size_t>(length));
        return true;
    }

    std::string JsonBody(std::initializer_list<std::pair<const char*, std::string>> values)
    {
        CJsonObject object(json_object_new_object());
        if (!object.Get())
            return {};

        for (const auto& [name, value] : values)
            json_object_object_add(object.Get(), name, json_object_new_string_len(value.data(), static_cast<int>(value.size())));
        return json_object_to_json_string_ext(object.Get(), JSON_C_TO_STRING_PLAIN);
    }

    bool IsAcceptedUrl(const std::string& url)
    {
        if (url.rfind("https://", 0) == 0)
            return true;

        // Plain HTTP is accepted only for a loopback development service.
        return url.rfind("http://127.0.0.1", 0) == 0 || url.rfind("http://localhost", 0) == 0;
    }
}

CNeonIdentityManager::CNeonIdentityManager()
{
    if (LoadSession())
    {
        m_signInState = ESignInState::SignedIn;
        m_statusText = "Neon account connected";
    }
}

CNeonIdentityManager::~CNeonIdentityManager()
{
    CNet*                             network = g_pCore ? g_pCore->GetNetwork() : nullptr;
    CNetHTTPDownloadManagerInterface* http = network ? network->GetHTTPDownloadManager(EDownloadMode::WEBBROWSER_LISTS) : nullptr;
    if (http)
        http->CancelDownload(this, StaticDownloadFinished);
}

void CNeonIdentityManager::DoPulse()
{
    CNet* network = g_pCore ? g_pCore->GetNetwork() : nullptr;
    if (!network)
        return;

    CNetHTTPDownloadManagerInterface* http = network->GetHTTPDownloadManager(EDownloadMode::WEBBROWSER_LISTS);
    if (!http)
        return;

    http->ProcessQueuedFiles();

    if (m_signInState == ESignInState::WaitingForBrowser && m_requestType == ERequestType::None && GetTickCount64_() >= m_nextPollAt)
    {
        const std::string body = JsonBody({{"flow_id", m_flowId}, {"poll_token", m_pollToken}});
        if (!QueueJsonRequest(ERequestType::AuthPoll, "/v1/auth/discord/poll", body, false))
            FailSignIn("Could not poll the Neon Identity service");
    }
}

bool CNeonIdentityManager::StartDiscordSignIn()
{
    if (IsAuthenticated() || IsSigningIn() || m_requestType != ERequestType::None)
        return false;

    m_lastError.clear();
    m_statusText = "Opening Discord...";
    m_signInState = ESignInState::Starting;
    if (!QueueJsonRequest(ERequestType::AuthStart, "/v1/auth/discord/start", "{}", false))
    {
        FailSignIn("Could not contact the Neon Identity service");
        return false;
    }
    return true;
}

void CNeonIdentityManager::SignOut()
{
    if (m_requestType == ERequestType::AuthStart || m_requestType == ERequestType::AuthPoll)
    {
        CNet*                             network = g_pCore ? g_pCore->GetNetwork() : nullptr;
        CNetHTTPDownloadManagerInterface* http = network ? network->GetHTTPDownloadManager(EDownloadMode::WEBBROWSER_LISTS) : nullptr;
        if (http)
            http->CancelDownload(this, StaticDownloadFinished);
        m_requestType = ERequestType::None;
    }

    CancelTicketPreparation();
    m_sessionToken.clear();
    m_flowId.clear();
    m_pollToken.clear();
    m_signInState = ESignInState::SignedOut;
    m_statusText = "Not signed in";
    m_lastError.clear();
    DeleteSession();
}

bool CNeonIdentityManager::IsSigningIn() const noexcept
{
    return m_signInState == ESignInState::Starting || m_signInState == ESignInState::WaitingForBrowser;
}

CNeonIdentityManager::EPrepareResult CNeonIdentityManager::PrepareConnectionTicket(const std::string& audience, const std::string& serverEndpoint)
{
    if (!IsAuthenticated())
        return EPrepareResult::Anonymous;

    if (!m_connectionTicket.empty() && m_ticketAudience == audience && m_ticketEndpoint == serverEndpoint)
        return EPrepareResult::Ready;

    if (m_ticketPreparationPending)
        return EPrepareResult::Pending;

    m_ticketAudience = audience;
    m_ticketEndpoint = serverEndpoint;
    m_connectionTicket.clear();
    m_ticketPreparationPending = true;
    m_ticketPreparationComplete = false;
    m_ticketPreparationSucceeded = false;
    m_lastError.clear();

    const std::string body = JsonBody({{"server_id", audience}, {"server_endpoint", serverEndpoint}});
    if (!QueueJsonRequest(ERequestType::Ticket, "/v1/tickets", body, true))
    {
        FinishTicketPreparation(false, "Could not contact the Neon Identity service");
        return EPrepareResult::Ready;
    }
    return EPrepareResult::Pending;
}

std::string CNeonIdentityManager::ConsumePreparedConnectionTicket()
{
    if (!m_ticketPreparationSucceeded)
        return {};

    std::string ticket = std::move(m_connectionTicket);
    CancelTicketPreparation();
    return ticket;
}

void CNeonIdentityManager::CancelTicketPreparation()
{
    if (m_requestType == ERequestType::Ticket)
    {
        CNet*                             network = g_pCore ? g_pCore->GetNetwork() : nullptr;
        CNetHTTPDownloadManagerInterface* http = network ? network->GetHTTPDownloadManager(EDownloadMode::WEBBROWSER_LISTS) : nullptr;
        if (http)
            http->CancelDownload(this, StaticDownloadFinished);
        m_requestType = ERequestType::None;
    }

    m_ticketPreparationPending = false;
    m_ticketPreparationComplete = false;
    m_ticketPreparationSucceeded = false;
    m_ticketAudience.clear();
    m_ticketEndpoint.clear();
    m_connectionTicket.clear();
}

void CNeonIdentityManager::StaticDownloadFinished(const SHttpDownloadResult& result)
{
    if (result.pObj)
        static_cast<CNeonIdentityManager*>(result.pObj)->DownloadFinished(result);
}

void CNeonIdentityManager::DownloadFinished(const SHttpDownloadResult& result)
{
    const ERequestType completedRequest = m_requestType;
    m_requestType = ERequestType::None;

    switch (completedRequest)
    {
        case ERequestType::AuthStart:
            HandleAuthStartResponse(result);
            break;
        case ERequestType::AuthPoll:
            HandleAuthPollResponse(result);
            break;
        case ERequestType::Ticket:
            HandleTicketResponse(result);
            break;
        default:
            break;
    }
}

bool CNeonIdentityManager::QueueJsonRequest(ERequestType requestType, const char* path, const std::string& body, bool authenticated)
{
    if (m_requestType != ERequestType::None || body.empty())
        return false;

    const std::string baseUrl = GetBaseUrl();
    if (!IsAcceptedUrl(baseUrl))
    {
        m_lastError = "The Neon Identity URL must use HTTPS";
        return false;
    }

    CNet*                             network = g_pCore ? g_pCore->GetNetwork() : nullptr;
    CNetHTTPDownloadManagerInterface* http = network ? network->GetHTTPDownloadManager(EDownloadMode::WEBBROWSER_LISTS) : nullptr;
    if (!http)
        return false;

    SHttpRequestOptions options;
    options.strRequestMethod = "POST";
    options.strPostData = body;
    options.uiConnectionAttempts = 1;
    options.uiConnectTimeoutMs = 10000;
    options.uiMaxRedirects = 0;
    options.requestHeaders["Content-Type"] = "application/json";
    options.requestHeaders["Accept"] = "application/json";
    if (authenticated)
        options.requestHeaders["Authorization"] = "Bearer " + m_sessionToken;

    m_requestType = requestType;
    if (!http->QueueFile((baseUrl + path).c_str(), nullptr, this, StaticDownloadFinished, options))
    {
        m_requestType = ERequestType::None;
        return false;
    }
    return true;
}

void CNeonIdentityManager::HandleAuthStartResponse(const SHttpDownloadResult& result)
{
    const CJsonObject response = ParseResponse(result);
    std::string       authorizationUrl;
    const std::string expectedAuthorizationPrefix = GetBaseUrl() + "/v1/auth/discord/authorize#";
    if (result.iErrorCode < 200 || result.iErrorCode >= 300 || !response.Get() || !ReadString(response.Get(), "flow_id", m_flowId, 128) ||
        !ReadString(response.Get(), "poll_token", m_pollToken, 256) || !ReadString(response.Get(), "authorization_url", authorizationUrl, 4096) ||
        authorizationUrl.rfind(expectedAuthorizationPrefix, 0) != 0)
    {
        FailSignIn("Neon Identity returned an invalid Discord authorization response");
        return;
    }

    if (!ShellExecuteNonBlocking("open", authorizationUrl.c_str()))
    {
        FailSignIn("Could not open Discord in your browser");
        return;
    }

    m_signInState = ESignInState::WaitingForBrowser;
    m_statusText = "Complete sign-in in your browser";
    m_nextPollAt = GetTickCount64_() + POLL_INTERVAL_MS;
}

void CNeonIdentityManager::HandleAuthPollResponse(const SHttpDownloadResult& result)
{
    const CJsonObject response = ParseResponse(result);
    std::string       status;
    if (result.iErrorCode >= 200 && result.iErrorCode < 300 && response.Get() && ReadString(response.Get(), "status", status, 32) && status == "pending")
    {
        m_nextPollAt = GetTickCount64_() + POLL_INTERVAL_MS;
        return;
    }

    std::string sessionToken;
    if (result.iErrorCode < 200 || result.iErrorCode >= 300 || !response.Get() || !ReadString(response.Get(), "session_token", sessionToken))
    {
        FailSignIn("Discord sign-in expired or was refused");
        return;
    }

    m_sessionToken = std::move(sessionToken);
    if (!SaveSession())
    {
        m_sessionToken.clear();
        FailSignIn("Neon could not securely save your session");
        return;
    }

    m_flowId.clear();
    m_pollToken.clear();
    m_signInState = ESignInState::SignedIn;
    m_statusText = "Neon account connected";
    m_lastError.clear();
}

void CNeonIdentityManager::HandleTicketResponse(const SHttpDownloadResult& result)
{
    const CJsonObject response = ParseResponse(result);
    std::string       ticket;
    if (result.iErrorCode == 401)
    {
        SignOut();
        FinishTicketPreparation(false, "Your Neon session is no longer valid");
        return;
    }

    if (result.iErrorCode < 200 || result.iErrorCode >= 300 || !response.Get() || !ReadString(response.Get(), "ticket", ticket))
    {
        FinishTicketPreparation(false, "Neon could not authorize this server connection");
        return;
    }

    m_connectionTicket = std::move(ticket);
    FinishTicketPreparation(true);
}

void CNeonIdentityManager::FailSignIn(const std::string& error)
{
    m_flowId.clear();
    m_pollToken.clear();
    m_signInState = ESignInState::Failed;
    m_statusText = "Discord sign-in failed";
    m_lastError = error;
    if (g_pCore)
        g_pCore->ShowMessageBox(_("Neon account"), error.c_str(), MB_BUTTON_OK | MB_ICON_ERROR);
}

void CNeonIdentityManager::FinishTicketPreparation(bool success, const std::string& error)
{
    m_ticketPreparationPending = false;
    m_ticketPreparationComplete = true;
    m_ticketPreparationSucceeded = success;
    if (!success)
    {
        m_connectionTicket.clear();
        m_lastError = error;
    }
}

bool CNeonIdentityManager::LoadSession()
{
    std::vector<char> envelope;
    if (!FileLoad(GetSessionPath(), envelope, static_cast<int>(MAX_TOKEN_BYTES + 1024)) || envelope.size() <= sizeof(SESSION_MAGIC) - 1 ||
        !std::equal(std::begin(SESSION_MAGIC), std::end(SESSION_MAGIC) - 1, envelope.begin()))
        return false;

    DATA_BLOB encrypted{static_cast<DWORD>(envelope.size() - (sizeof(SESSION_MAGIC) - 1)),
                        reinterpret_cast<BYTE*>(envelope.data() + sizeof(SESSION_MAGIC) - 1)};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(DPAPI_ENTROPY) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(DPAPI_ENTROPY))};
    DATA_BLOB plaintext{};
    if (!CryptUnprotectData(&encrypted, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plaintext))
        return false;

    const bool valid = plaintext.cbData > 0 && plaintext.cbData <= MAX_TOKEN_BYTES;
    if (valid)
        m_sessionToken.assign(reinterpret_cast<const char*>(plaintext.pbData), plaintext.cbData);
    if (plaintext.pbData)
        LocalFree(plaintext.pbData);
    return valid;
}

bool CNeonIdentityManager::SaveSession() const
{
    if (m_sessionToken.empty() || m_sessionToken.size() > MAX_TOKEN_BYTES)
        return false;

    DATA_BLOB plaintext{static_cast<DWORD>(m_sessionToken.size()), reinterpret_cast<BYTE*>(const_cast<char*>(m_sessionToken.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(DPAPI_ENTROPY) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(DPAPI_ENTROPY))};
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&plaintext, L"Neon Identity session", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
        return false;

    std::vector<unsigned char> envelope;
    envelope.insert(envelope.end(), SESSION_MAGIC, SESSION_MAGIC + sizeof(SESSION_MAGIC) - 1);
    envelope.insert(envelope.end(), encrypted.pbData, encrypted.pbData + encrypted.cbData);
    LocalFree(encrypted.pbData);
    return FileSave(GetSessionPath(), envelope.data(), static_cast<unsigned long>(envelope.size()));
}

void CNeonIdentityManager::DeleteSession() const
{
    FileDelete(GetSessionPath());
}

std::string CNeonIdentityManager::GetSessionPath() const
{
    const SString relativePath = g_pCore ? g_pCore->GetClientProfilePath("mta/config/neon_identity.dat") : SStringX("mta/config/neon_identity.dat");
    return CalcMTASAPath(relativePath);
}

std::string CNeonIdentityManager::GetBaseUrl() const
{
    std::string baseUrl = CVARS_GET_VALUE<std::string>("neon_identity_url");
    while (!baseUrl.empty() && baseUrl.back() == '/')
        baseUrl.pop_back();
    return baseUrl;
}
